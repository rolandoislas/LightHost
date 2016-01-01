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
#include <ctime>
#if JUCE_WINDOWS
#include "Windows.h"
#endif

#if !JUCE_MAC
class IconMenu::PluginListWindow : public DocumentWindow
{
public:
	PluginListWindow(IconMenu& owner_, AudioPluginFormatManager& pluginFormatManager)
		: DocumentWindow("Available Plugins", Colours::white,
			DocumentWindow::minimiseButton | DocumentWindow::closeButton),
		owner(owner_)
	{
		const File deadMansPedalFile(getAppProperties().getUserSettings()
			->getFile().getSiblingFile("RecentlyCrashedPluginsList"));

		setContentOwned(new PluginListComponent(pluginFormatManager,
			owner.knownPluginList,
			deadMansPedalFile,
			getAppProperties().getUserSettings()), true);

		setUsingNativeTitleBar(true);
		setResizable(true, false);
		setResizeLimits(300, 400, 800, 1500);
		setTopLeftPosition(60, 60);

		restoreWindowStateFromString(getAppProperties().getUserSettings()->getValue("listWindowPos"));
		setVisible(true);
	}

	~PluginListWindow()
	{
		getAppProperties().getUserSettings()->setValue("listWindowPos", getWindowStateAsString());

		clearContentComponent();
	}

	void closeButtonPressed()
	{
		owner.pluginListWindow = nullptr;
	}

private:
	IconMenu& owner;
};
#endif

IconMenu::IconMenu()
{
    // Initiialization
    formatManager.addDefaultFormats();
	#if JUCE_WINDOWS
	x = y = 0;
	#endif
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
	#if JUCE_MAC
	if (exec("defaults read -g AppleInterfaceStyle").compare("Dark") == 1)
		setIconImage(ImageFileFormat::loadFrom(BinaryData::menu_icon_white_png, BinaryData::menu_icon_white_pngSize));
	else
		setIconImage(ImageFileFormat::loadFrom(BinaryData::menu_icon_png, BinaryData::menu_icon_pngSize));
	#else
	setIconImage(ImageFileFormat::loadFrom(BinaryData::menu_icon_png, BinaryData::menu_icon_pngSize));
	#endif
	setIconTooltip(JUCEApplication::getInstance()->getApplicationName());
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
	int pluginTime = 0;
    for (int i = 0; i < activePluginList.getNumTypes(); i++)
    {
        PluginDescription plugin = getNextPluginOlderThanTime(pluginTime);
        String errorMessage;
        AudioPluginInstance* instance = formatManager.createPluginInstance(plugin, graph.getSampleRate(), graph.getBlockSize(), errorMessage);
		String pluginUid;
		pluginUid << "pluginState-" << i;
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

PluginDescription IconMenu::getNextPluginOlderThanTime(int &time)
{
	int timeStatic = time;
	PluginDescription closest;
	int diff = INT_MAX;
	for (int i = 0; i < activePluginList.getNumTypes(); i++)
	{
		PluginDescription plugin = *activePluginList.getType(i);
		String key = "pluginOrder-" + plugin.descriptiveName + plugin.version + plugin.pluginFormatName;
		String pluginTimeString = getAppProperties().getUserSettings()->getValue(key);
		int pluginTime = atoi(pluginTimeString.toStdString().c_str());
		if (pluginTime > timeStatic && abs(timeStatic - pluginTime) < diff)
		{
			diff = abs(timeStatic - pluginTime);
			closest = plugin;
			time = pluginTime;
		}
	}
	return closest;
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
        }
    }
}

#if JUCE_MAC
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
#endif

void IconMenu::timerCallback()
{
    stopTimer();
    menu.clear();
    menu.addSectionHeader(JUCEApplication::getInstance()->getApplicationName());
    if (menuIconLeftClicked) {
        menu.addItem(1, "Preferences");
        menu.addItem(2, "Edit Plugins");
        menu.addSeparator();
        // Active plugins
		int time = 0;
        for (int i = 0; i < activePluginList.getNumTypes(); i++)
        {
            PopupMenu options;
            options.addItem(i+3, "Edit");
            options.addItem(activePluginList.getNumTypes()+i+3, "Delete");
            // TODO bypass
			PluginDescription plugin = getNextPluginOlderThanTime(time);
            menu.addSubMenu(plugin.name, options);
        }
        menu.addSeparator();
        // All plugins
        knownPluginList.addToMenu(menu, pluginSortMethod);
    }
    else
    {
        menu.addItem(1, "Quit");
    }
	#if JUCE_MAC || JUCE_LINUX
    menu.showMenuAsync(PopupMenu::Options().withTargetComponent(this), ModalCallbackFunction::forComponent(menuInvocationCallback, this));
	#else
	if (x == 0 || y == 0)
	{
		POINT iconLocation;
		iconLocation.x = 0;
		iconLocation.y = 0;
		GetCursorPos(&iconLocation);
		x = iconLocation.x;
		y = iconLocation.y;
	}
	juce::Rectangle<int> rect(x, y, 1, 1);
	menu.showMenuAsync(PopupMenu::Options().withTargetScreenArea(rect), ModalCallbackFunction::forComponent(menuInvocationCallback, this));
	#endif
}

void IconMenu::mouseDown(const MouseEvent& e)
{
	#if JUCE_MAC
		Process::setDockIconVisible(true);
	#endif
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
	#if JUCE_MAC
    // Click elsewhere
    if (id == 0 && !PluginWindow::containsActiveWindows())
        Process::setDockIconVisible(false);
	#endif
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

			int index = id - im->activePluginList.getNumTypes() - 3;
			PluginDescription plugin = *im->activePluginList.getType(index);
			String key = "pluginOrder-" + plugin.descriptiveName + plugin.version + plugin.pluginFormatName;
			getAppProperties().getUserSettings()->removeValue(key);
			getAppProperties().saveIfNeeded();
            im->activePluginList.removeType(index);

			im->savePluginStates();
			im->loadActivePlugins();
        }
        // Add plugin
        else if (id > im->activePluginList.getNumTypes() + 2)
        {
            im->deletePluginStates();

			PluginDescription plugin = *im->knownPluginList.getType(im->knownPluginList.getIndexChosenByMenu(id));
			String key = "pluginOrder-" + plugin.descriptiveName + plugin.version + plugin.pluginFormatName;
			getAppProperties().getUserSettings()->setValue(key, time(0));
			getAppProperties().saveIfNeeded();
            im->activePluginList.addType(plugin);

			im->savePluginStates();
			im->loadActivePlugins();
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
		String pluginUid;
		pluginUid << "pluginState-" << i;
        getAppProperties().getUserSettings()->removeValue(pluginUid);
        getAppProperties().saveIfNeeded();
    }
}

void IconMenu::savePluginStates()
{
    for (int i = 0; i < activePluginList.getNumTypes(); i++)
    {
		AudioProcessorGraph::Node* node = graph.getNodeForId(i+3);
		if (node == nullptr)
			break;
        AudioProcessor& processor = *node->getProcessor();
		String pluginUid;
		pluginUid << "pluginState-" << i;
        MemoryBlock savedStateBinary;
        processor.getStateInformation(savedStateBinary);
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
	if (pluginListWindow == nullptr)
		pluginListWindow = new PluginListWindow(*this, formatManager);
	pluginListWindow->toFront(true);
	removePluginsLackingInputOutput();
}

void IconMenu::removePluginsLackingInputOutput()
{
	std::vector<int> removeIndex;
	for (int i = 0; i < knownPluginList.getNumTypes(); i++)
	{
		PluginDescription* plugin = knownPluginList.getType(i);
		if (plugin->numInputChannels < 2 || plugin->numOutputChannels < 2)
			removeIndex.push_back(i);
	}
	for (int i = 0; i < removeIndex.size(); i++)
		knownPluginList.removeType(removeIndex[i] - i);
}