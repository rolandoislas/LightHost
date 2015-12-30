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
    static void doReload(int id, IconMenu*);
    void changeListenerCallback(ChangeBroadcaster* changed);
private:
    std::string exec(const char* cmd);
    void timerCallback();
    void reloadPlugins();
    void showAudioSettings();
    void loadActivePlugins();
    void savePluginStates();
    void deletePluginStates();
    
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
    
    class ScanThread;
};

#endif /* IconMenu_hpp */
