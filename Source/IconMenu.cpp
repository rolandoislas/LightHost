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
        owner.removePluginsLackingInputOutput();
        #if JUCE_MAC
        Process::setDockIconVisible(false);
        #endif
		owner.pluginListWindow = nullptr;
	}

private:
	IconMenu& owner;
};

IconMenu::IconMenu() : INDEX_EDIT(1000000), INDEX_BYPASS(2000000), INDEX_DELETE(3000000)
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
	savePluginStates();
}

void IconMenu::loadActivePlugins()
{
	const int INPUT = 1000000;
	const int OUTPUT = INPUT + 1;
	const int CHANNEL_ONE = 0;
	const int CHANNEL_TWO = 1;
	PluginWindow::closeAllCurrentlyOpenWindows();
    graph.clear();
    inputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode), INPUT);
    outputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode), OUTPUT);
    if (activePluginList.getNumTypes() == 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
    }
	int pluginTime = 0;
	int lastId = 0;
	bool hasInputConnected = false;
	// NOTE: Node ids cannot begin at 0.
    for (int i = 1; i <= activePluginList.getNumTypes(); i++)
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
        graph.addNode(instance, i);
		String key = "pluginBypass-" + plugin.descriptiveName + plugin.version + plugin.pluginFormatName;
		bool bypass = getAppProperties().getUserSettings()->getBoolValue(key, false);
        // Input to plugin
        if ((!hasInputConnected) && (!bypass))
        {
            graph.addConnection(INPUT, CHANNEL_ONE, i, CHANNEL_ONE);
            graph.addConnection(INPUT, CHANNEL_TWO, i, CHANNEL_TWO);
			hasInputConnected = true;
        }
        // Connect previous plugin to current
        else if (!bypass)
        {
            graph.addConnection(lastId, CHANNEL_ONE, i, CHANNEL_ONE);
            graph.addConnection(lastId, CHANNEL_TWO, i, CHANNEL_TWO);
        }
		if (!bypass)
			lastId = i;
    }
	if (lastId > 0)
	{
		// Last active plugin to output
		graph.addConnection(lastId, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
		graph.addConnection(lastId, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
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
            options.addItem(INDEX_EDIT + i, "Edit");
			std::vector<PluginDescription> timeSorted = getTimeSortedList();
			String key = "pluginBypass-" + timeSorted[i].descriptiveName + timeSorted[i].version + timeSorted[i].pluginFormatName;
			bool bypass = getAppProperties().getUserSettings()->getBoolValue(key);
			options.addItem(INDEX_BYPASS + i, "Bypass", true, bypass);
            options.addItem(INDEX_DELETE + i, "Delete");
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
        if (id >= im->INDEX_DELETE && id < im->INDEX_DELETE + 1000000)
        {
            im->deletePluginStates();

			int index = id - im->INDEX_DELETE;
			std::vector<PluginDescription> timeSorted = im->getTimeSortedList();
			String key = "pluginOrder-" + timeSorted[index].descriptiveName + timeSorted[index].version + timeSorted[index].pluginFormatName;
			int unsortedIndex = 0;
			for (int i = 0; im->activePluginList.getNumTypes(); i++)
			{
				PluginDescription current = *im->activePluginList.getType(i);
				if (key.equalsIgnoreCase("pluginOrder-" + current.descriptiveName + current.version + current.pluginFormatName))
				{
					unsortedIndex = i;
					break;
				}
			}

			// Remove plugin order
			getAppProperties().getUserSettings()->removeValue(key);
			// Remove bypass entry
			getAppProperties().getUserSettings()->removeValue(key.replace("Order", "Bypass"));
			getAppProperties().saveIfNeeded();
			
			// Remove plugin from list
            im->activePluginList.removeType(unsortedIndex);

			// Save current states
			im->savePluginStates();
			im->loadActivePlugins();
        }
        // Add plugin
        else if (im->knownPluginList.getIndexChosenByMenu(id) > -1)
        {
            im->deletePluginStates();

			PluginDescription plugin = *im->knownPluginList.getType(im->knownPluginList.getIndexChosenByMenu(id));
			String key = "pluginOrder-" + plugin.descriptiveName + plugin.version + plugin.pluginFormatName;
			int t = time(0);
			getAppProperties().getUserSettings()->setValue(key, t);
			getAppProperties().saveIfNeeded();
            im->activePluginList.addType(plugin);

			im->savePluginStates();
			im->loadActivePlugins();
        }
		// Bypass plugin
		else if (id >= im->INDEX_BYPASS && id < im->INDEX_BYPASS + 1000000)
		{
			im->deletePluginStates();

			int index = id - im->INDEX_BYPASS;
			std::vector<PluginDescription> timeSorted = im->getTimeSortedList();
			String key = "pluginBypass-" + timeSorted[index].descriptiveName + timeSorted[index].version + timeSorted[index].pluginFormatName;

			// Set bypass flag
			bool bypassed = getAppProperties().getUserSettings()->getBoolValue(key);
			getAppProperties().getUserSettings()->setValue(key, !bypassed);
			getAppProperties().saveIfNeeded();

			im->savePluginStates();
			im->loadActivePlugins();
		}
        // Show active plugin GUI
		else if (id >= im->INDEX_EDIT && id < im->INDEX_EDIT + 1000000)
        {
            if (const AudioProcessorGraph::Node::Ptr f = im->graph.getNodeForId(id - im->INDEX_EDIT + 1))
                if (PluginWindow* const w = PluginWindow::getWindowFor(f, PluginWindow::Normal))
                    w->toFront(true);
        }
        // Update menu
        im->startTimer(50);
    }
}

std::vector<PluginDescription> IconMenu::getTimeSortedList()
{
	int time = 0;
	std::vector<PluginDescription> list;
	for (int i = 0; i < activePluginList.getNumTypes(); i++)
		list.push_back(getNextPluginOlderThanTime(time));
	return list;
		
}

void IconMenu::deletePluginStates()
{
    for (int i = 1; i <= activePluginList.getNumTypes(); i++)
    {
		String pluginUid;
		pluginUid << "pluginState-" << i;
        getAppProperties().getUserSettings()->removeValue(pluginUid);
        getAppProperties().saveIfNeeded();
    }
}

void IconMenu::savePluginStates()
{
    for (int i = 1; i <= activePluginList.getNumTypes(); i++)
    {
		AudioProcessorGraph::Node* node = graph.getNodeForId(i);
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