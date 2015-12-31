//
//  IconMenu.hpp
//  Light Host
//
//  Created by Rolando Islas on 12/26/15.
//
//

#ifndef IconMenu_hpp
#define IconMenu_hpp

ApplicationProperties& getAppProperties();

class IconMenu : public SystemTrayIconComponent, private Timer, public ChangeListener
{
public:
    IconMenu();
    ~IconMenu();
    void mouseDown(const MouseEvent&);
    static void menuInvocationCallback(int id, IconMenu*);
    static void doReloadWithDefaultLocations(int id, IconMenu*);
    void changeListenerCallback(ChangeBroadcaster* changed);
private:
	#if JUCE_MAC
    std::string exec(const char* cmd);
	#endif
    void timerCallback();
    void reloadPlugins();
    void showAudioSettings();
    void loadActivePlugins();
    void savePluginStates();
    void deletePluginStates();
	PluginDescription getNextPluginOlderThanTime(int &time);
	void removePluginsLackingInputOutput();
    
    AudioDeviceManager deviceManager;
    AudioPluginFormatManager formatManager;
    KnownPluginList knownPluginList;
    KnownPluginList activePluginList;
    KnownPluginList::SortMethod pluginSortMethod;
    PopupMenu menu;
    ScopedPointer<PluginDirectoryScanner> scanner;
    bool menuIconLeftClicked;
    AudioProcessorGraph graph;
    AudioProcessorPlayer player;
    AudioProcessorGraph::Node *inputNode;
    AudioProcessorGraph::Node *outputNode;
	#if !JUCE_MAC
	class PluginListWindow;
	ScopedPointer<PluginListWindow> pluginListWindow;
	#endif
};

#endif /* IconMenu_hpp */
