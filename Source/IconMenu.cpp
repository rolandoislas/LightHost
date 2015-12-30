//
//  IconMenu.cpp
//  Light Host
//
//  Created by Rolando Islas on 12/26/15.
//
//

#include "../JuceLibraryCode/JuceHeader.h"
#include "IconMenu.hpp"
#include "PluginWindow.h"

IconMenu::IconMenu()
{
    // Initiialization
    formatManager.addDefaultFormats();
    // Audio device
    ScopedPointer<XmlElement> savedAudioState (getAppProperties().getUserSettings()->getXmlValue("audioDeviceState"));
    deviceManager.initialise(256, 256, savedAudioState, true);
    player.setProcessor(&graph);
    deviceManager.addAudioCallback(&player);
    // Plugins - all
    ScopedPointer<XmlElement> savedPluginList(getAppProperties().getUserSettings()->getXmlValue("pluginList"));
    if (savedPluginList != nullptr)
        knownPluginList.recreateFromXml(*savedPluginList);
    pluginSortMethod = KnownPluginList::sortByManufacturer;
    knownPluginList.addChangeListener(this);
    // Plugins - active
    ScopedPointer<XmlElement> savedPluginListActive(getAppProperties().getUserSettings()->getXmlValue("pluginListActive"));
    if (savedPluginListActive != nullptr)
        activePluginList.recreateFromXml(*savedPluginListActive);
    loadActivePlugins();
    activePluginList.addChangeListener(this);
    // Set menu icon
    if (exec("defaults read -g AppleInterfaceStyle").compare("Dark") == 1)
        setIconImage(ImageFileFormat::loadFrom(BinaryData::menu_icon_white_png, BinaryData::menu_icon_white_pngSize));
    else
        setIconImage(ImageFileFormat::loadFrom(BinaryData::menu_icon_png, BinaryData::menu_icon_pngSize));
};

IconMenu::~IconMenu()
{
    
}

void IconMenu::loadActivePlugins()
{
    graph.clear();
    inputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode), 1);
     outputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode), 2);
    if (activePluginList.getNumTypes() == 0)
    {
        graph.addConnection(1, 0, 2, 0);
        graph.addConnection(1, 1, 2, 1);
    }
    for (int i = 0; i < activePluginList.getNumTypes(); i++)
    {
        PluginDescription plugin = *activePluginList.getType(i);
        String errorMessage;
        AudioPluginInstance* instance = formatManager.createPluginInstance(plugin, graph.getSampleRate(), graph.getBlockSize(), errorMessage);
        String pluginUid = "pluginState-" + std::to_string(i);
        String savedPluginState = getAppProperties().getUserSettings()->getValue(pluginUid);
        MemoryBlock savedPluginBinary;
        savedPluginBinary.fromBase64Encoding(savedPluginState);
        instance->setStateInformation(savedPluginBinary.getData(), savedPluginBinary.getSize());
        graph.addNode(instance, i+3);
        // Input to plugin
        if (i == 0)
        {
            graph.addConnection(1, 0, i+3, 0);
            graph.addConnection(1, 1, i+3, 1);
        }
        // Plugin to output
        if (i == activePluginList.getNumTypes() - 1)
        {
            graph.addConnection(i+3, 0, 2, 0);
            graph.addConnection(i+3, 1, 2, 1);
        }
        // Connect previous plugin to current
        if (i > 0)
        {
            graph.addConnection(i+2, 0, i+3, 0);
            graph.addConnection(i+2, 1, i+3, 1);
        }
    }
}

void IconMenu::changeListenerCallback(ChangeBroadcaster* changed)
{
    if (changed == &knownPluginList)
    {
        ScopedPointer<XmlElement> savedPluginList (knownPluginList.createXml());
        if (savedPluginList != nullptr)
        {
            getAppProperties().getUserSettings()->setValue ("pluginList", savedPluginList);
            getAppProperties().saveIfNeeded();
        }
    }
    else if (changed == &activePluginList)
    {
        ScopedPointer<XmlElement> savedPluginList (activePluginList.createXml());
        if (savedPluginList != nullptr)
        {
            getAppProperties().getUserSettings()->setValue ("pluginListActive", savedPluginList);
            getAppProperties().saveIfNeeded();
            loadActivePlugins();
        }
    }
}

std::string IconMenu::exec(const char* cmd)
{
    std::shared_ptr<FILE> pipe(popen(cmd, "r"), pclose);
    if (!pipe) return "ERROR";
    char buffer[128];
    std::string result = "";
    while (!feof(pipe.get()))
    {
        if (fgets(buffer, 128, pipe.get()) != NULL)
            result += buffer;
    }
    return result;
}

void IconMenu::timerCallback()
{
    stopTimer();
    menu.clear();
    menu.addSectionHeader(JUCEApplication::getInstance()->getApplicationName());
    if (menuIconLeftClicked) {
        menu.addItem(1, "Preferences");
        menu.addItem(2, "Reload Plugins");
        menu.addSeparator();
        // Active plugins
        for (int i = 0; i < activePluginList.getNumTypes(); i++)
        {
            PopupMenu options;
            options.addItem(i+3, "Edit");
            options.addItem(activePluginList.getNumTypes()+i+3, "Delete");
            // TODO bypass
            menu.addSubMenu(activePluginList.getType(i)->name, options);
        }
        menu.addSeparator();
        // All plugins
        knownPluginList.addToMenu(menu, pluginSortMethod);
    }
    else
    {
        menu.addItem(1, "Quit");
    }
    menu.showMenuAsync(PopupMenu::Options().withTargetComponent(this), ModalCallbackFunction::forComponent(menuInvocationCallback, this));
}

void IconMenu::mouseDown(const MouseEvent& e)
{
    Process::setDockIconVisible(true);
    Process::makeForegroundProcess();
    menuIconLeftClicked = e.mods.isLeftButtonDown();
    startTimer(50);
}

void IconMenu::menuInvocationCallback(int id, IconMenu* im)
{
    // Right click
    if ((!im->menuIconLeftClicked) && id == 1)
    {
        im->savePluginStates();
        return JUCEApplication::getInstance()->quit();
    }
    // Click elsewhere
    if (id == 0 && !PluginWindow::containsActiveWindows())
        Process::setDockIconVisible(false);
    // Audio settings
    if (id == 1)
        im->showAudioSettings();
    // Reload
    if (id == 2)
        im->reloadPlugins();
    // Plugins
    if (id > 2)
    {
        // Delete plugin
        if (id > im->activePluginList.getNumTypes() + 2 && id <= im->activePluginList.getNumTypes() * 2 + 2)
        {
            im->deletePluginStates();
            im->activePluginList.removeType(id - im->activePluginList.getNumTypes() - 3);
            
        }
        // Add plugin
        else if (id > im->activePluginList.getNumTypes() + 2)
        {
            im->deletePluginStates();
            im->activePluginList.addType(*im->knownPluginList.getType(im->knownPluginList.getIndexChosenByMenu(id)));
        }
        // Show active plugin GUI
        else
        {
            if (const AudioProcessorGraph::Node::Ptr f = im->graph.getNodeForId(id))
                if (PluginWindow* const w = PluginWindow::getWindowFor(f, PluginWindow::Normal))
                    w->toFront(true);
        }
        // Update menu
        im->startTimer(50);
    }
}

void IconMenu::deletePluginStates()
{
    for (int i = 0; i < activePluginList.getNumTypes(); i++)
    {
        String pluginUid = "pluginState-" + std::to_string(i);
        getAppProperties().getUserSettings()->removeValue(pluginUid);
        getAppProperties().saveIfNeeded();
    }
}

void IconMenu::savePluginStates()
{
    for (int i = 0; i < activePluginList.getNumTypes(); i++)
    {
        AudioProcessor& processor = *graph.getNodeForId(i+3)->getProcessor();
        String pluginUid = "pluginState-" + std::to_string(i);
        MemoryBlock savedStateBinary;
        processor.getStateInformation(savedStateBinary);
        ScopedPointer<XmlElement> savedStateXml(XmlElement::createTextElement(savedStateBinary.toBase64Encoding()));
        getAppProperties().getUserSettings()->setValue(pluginUid, savedStateBinary.toBase64Encoding());
        getAppProperties().saveIfNeeded();
    }
}

void IconMenu::showAudioSettings()
{
    AudioDeviceSelectorComponent audioSettingsComp (deviceManager, 0, 256, 0, 256, false, false, true, true);
    audioSettingsComp.setSize(500, 450);
    
    DialogWindow::LaunchOptions o;
    o.content.setNonOwned(&audioSettingsComp);
    o.dialogTitle                   = "Audio Settings";
    o.componentToCentreAround       = this;
    o.dialogBackgroundColour        = Colour::fromRGB(236, 236, 236);
    o.escapeKeyTriggersCloseButton  = true;
    o.useNativeTitleBar             = true;
    o.resizable                     = false;

    o.runModal();
        
    ScopedPointer<XmlElement> audioState(deviceManager.createStateXml());
        
    getAppProperties().getUserSettings()->setValue("audioDeviceState", audioState);
    getAppProperties().getUserSettings()->saveIfNeeded();
}

void IconMenu::reloadPlugins()
{
    NativeMessageBox::showOkCancelBox(AlertWindow::AlertIconType::InfoIcon, "Reload Plugins?", "Confirm scan and load of any new or updated plugins.", this, ModalCallbackFunction::forComponent(doReload, this));
}

void IconMenu::doReload(int id, IconMenu* im)
{
    // Canceled
    if (id == 0)
        return Process::setDockIconVisible(false);
    // Scan
    const File deadMansPedalFile (getAppProperties().getUserSettings()->getFile().getSiblingFile("RecentlyCrashedPluginsList"));
    String pluginName;
    for (int i = 0; i < im->formatManager.getNumFormats(); i++)
    {
        im->scanner = new PluginDirectoryScanner(im->knownPluginList, *im->formatManager.getFormat(i), im->formatManager.getFormat(i)->getDefaultLocationsToSearch(), true, deadMansPedalFile);
        while (im->scanner->scanNextFile(true, pluginName)) { }
    }
    // Remove plugins without inputs and/or outputs
    std::vector<int> removeIndex;
    for (int i = 0; i < im->knownPluginList.getNumTypes(); i++)
    {
        PluginDescription* plugin = im->knownPluginList.getType(i);
        if (plugin->numInputChannels < 2 || plugin->numOutputChannels < 2)
            removeIndex.push_back(i);
    }
    for (int i = 0; i < removeIndex.size(); i++)
        im->knownPluginList.removeType(removeIndex[i] - i);
    // Finish
    NativeMessageBox::showMessageBox(AlertWindow::AlertIconType::InfoIcon, "Completed", "Plugins have been refreshed.");
    Process::setDockIconVisible(false);
}