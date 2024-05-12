/*
 // Copyright (c) 2021-2022 Timothy Schoen
 // For information on usage and redistribution, and for a DISCLAIMER OF ALL
 // WARRANTIES, see the file, "LICENSE.txt," in this distribution.
 */

#pragma once

#include <m_pd.h>

#include <readerwriterqueue.h>
#include "Constants.h"
#include "Objects/AllGuis.h"
#include "Iolet.h"       // Move to impl
#include "Pd/Instance.h" // Move to impl
#include "Pd/MessageListener.h"
#include "Utility/RateReducer.h"
#include "Utility/ModifierKeyListener.h"
#include "Utility/NVGComponent.h"

using PathPlan = std::vector<Point<float>>;

class Canvas;
class PathUpdater;

class Connection : public DrawablePath
    , public ComponentListener
    , public ChangeListener
    , public pd::MessageListener 
    , public NVGComponent
    , public NVGContextListener
    , public MultiTimer
{
public:
    int inIdx;
    int outIdx;
    int numSignalChannels = 1;

    WeakReference<Iolet> inlet, outlet;
    WeakReference<Object> inobj, outobj;

    Path toDrawLocalSpace;
    String lastId;

    std::atomic<int> messageActivity;

    Connection(Canvas* parent, Iolet* start, Iolet* end, t_outconnect* oc);
    ~Connection() override;

    void updateOverlays(int overlay);

    static void renderConnectionPath(Graphics& g,
        Canvas* cnv,
        Path const& connectionPath,
        bool isSignal,
        bool isGemState,
        bool isMouseOver = false,
        bool showDirection = false,
        bool showConnectionOrder = false,
        bool isSelected = false,
        Point<int> mousePos = { 0, 0 },
        bool isHovering = false,
        int connections = 0,
        int connectionNum = 0,
        int numSignalChannels = 0);

    static Path getNonSegmentedPath(Point<float> start, Point<float> end);

    bool isSegmented() const;
    void setSegmented(bool segmented);
    
    bool intersectsRectangle(Rectangle<int> rectToIntersect);
        
    void render(NVGcontext* nvg) override;
    void nvgContextDeleted(NVGcontext* nvg) override;

    void updatePath();

    void forceUpdate();

    void lookAndFeelChanged() override;

    void changeListenerCallback(ChangeBroadcaster* source) override;

    bool hitTest(int x, int y) override;

    void mouseDown(MouseEvent const& e) override;
    void mouseMove(MouseEvent const& e) override;
    void mouseDrag(MouseEvent const& e) override;
    void mouseUp(MouseEvent const& e) override;
    void mouseEnter(MouseEvent const& e) override;
    void mouseExit(MouseEvent const& e) override;

    Point<float> getStartPoint() const;
    Point<float> getEndPoint() const;

    void reconnect(Iolet* target);

    bool intersects(Rectangle<float> toCheck, int accuracy = 4) const;
    int getClosestLineIdx(Point<float> const& position, PathPlan const& plan);

    void setPointer(t_outconnect* ptr);
    t_outconnect* getPointer();

    t_symbol* getPathState();
    void pushPathState();
    void popPathState();

    void componentMovedOrResized(Component& component, bool wasMoved, bool wasResized) override;

    // Pathfinding
    int findLatticePaths(PathPlan& bestPath, PathPlan& pathStack, Point<float> start, Point<float> end, Point<float> increment);

    void findPath();

    void applyBestPath();

    bool intersectsObject(Object* object) const;
    bool straightLineIntersectsObject(Line<float> toCheck, Array<Object*>& objects);

    void receiveMessage(t_symbol* symbol, pd::Atom const atoms[8], int numAtoms) override;

    bool isSelected() const;

    StringArray getMessageFormated();
    int getSignalData(t_float* output, int maxChannels);

private:

    enum Timer { StopAnimation, Animation };

    void timerCallback(int ID) override;

    void animate();

    int getMultiConnectNumber();
    int getNumSignalChannels();
    int getNumberOfConnections();
    
    void setSelected(bool shouldBeSelected);

    Array<SafePointer<Connection>> reconnecting;
    Rectangle<float> startReconnectHandle, endReconnectHandle, endCableOrderDisplay;

    bool selectedFlag = false;
    bool segmented = false;
    bool isHovering = false;
    
    PathPlan currentPlan;

    Value locked;
    Value presentationMode;

    bool showDirection = false;
    bool showConnectionOrder = false;
    bool showActivity = false;
    
    NVGcolor baseColour;
    NVGcolor dataColour;
    NVGcolor signalColour;
    NVGcolor handleColour;
    NVGcolor shadowColour;
    NVGcolor outlineColour;
    NVGcolor gemColour;

    NVGcolor textColour;
    
    RectangleList<int> clipRegion;
    
    enum CableType { DataCable, GemCable, SignalCable, MultichannelCable };
    CableType cableType;

    Canvas* cnv;

    Point<float> previousPStart = Point<float>();

    int dragIdx = -1;

    float mouseDownPosition = 0;

    int cacheId = -1;
    bool cachedIsValid;

    pd::WeakReference ptr;

    pd::Atom lastValue[8];
    int lastNumArgs = 0;
    t_symbol* lastSelector = nullptr;

    float offset = 0.0f;

    friend class ConnectionPathUpdater;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Connection)
};

class ConnectionBeingCreated : public DrawablePath, public NVGComponent {
    SafePointer<Iolet> iolet;
    Component* cnv;

public:
    ConnectionBeingCreated(Iolet* target, Component* canvas)
        : NVGComponent(this)
        , iolet(target)
        , cnv(canvas)
    {
        setStrokeThickness(5.0f);
        
        // Only listen for mouse-events on canvas and the original iolet
        setInterceptsMouseClicks(false, true);
        cnv->addMouseListener(this, true);
        iolet->addMouseListener(this, false);

        cnv->addAndMakeVisible(this);

        setAlwaysOnTop(true);
        setAccessible(false); // TODO: implement accessibility. We disable default, since it makes stuff slow on macOS
    }

    ~ConnectionBeingCreated() override
    {
        cnv->removeMouseListener(this);
        iolet->removeMouseListener(this);
    }

    void mouseDrag(MouseEvent const& e) override
    {
        mouseMove(e);
    }

    void mouseMove(MouseEvent const& e) override
    {
        if (rateReducer.tooFast())
            return;

        auto ioletPoint = cnv->getLocalPoint((Component*)iolet->object, iolet->getBounds().toFloat().getCentre());
        auto cursorPoint = e.getEventRelativeTo(cnv).position;

        auto& startPoint = iolet->isInlet ? cursorPoint : ioletPoint;
        auto& endPoint = iolet->isInlet ? ioletPoint : cursorPoint;

        auto connectionPath = Connection::getNonSegmentedPath(startPoint.toFloat(), endPoint.toFloat());
        setPath(connectionPath);
        
        repaint();
        iolet->repaint();
    }
    
    void render(NVGcontext* nvg) override
    {
        auto lineColour = cnv->findColour(PlugDataColour::dataColourId).brighter(0.6f);
        auto shadowColour = findColour(PlugDataColour::canvasBackgroundColourId).contrasting(0.06f).withAlpha(0.24f);

        nvgSave(nvg);
        setJUCEPath(nvg, getPath());
        nvgStrokePaint(nvg, nvgDoubleStroke(nvg, convertColour(lineColour), convertColour(shadowColour)));
        nvgStrokeWidth(nvg, 4.0f);
        nvgStroke(nvg);
        nvgRestore(nvg);
    }

    Iolet* getIolet()
    {
        return iolet;
    }

    RateReducer rateReducer = RateReducer(90);
};

// Helper class to group connection path changes together into undoable/redoable actions
class ConnectionPathUpdater : public Timer {
    Canvas* canvas;

    moodycamel::ReaderWriterQueue<std::pair<Component::SafePointer<Connection>, t_symbol*>> connectionUpdateQueue = moodycamel::ReaderWriterQueue<std::pair<Component::SafePointer<Connection>, t_symbol*>>(4096);

    void timerCallback() override;

public:
    explicit ConnectionPathUpdater(Canvas* cnv)
        : canvas(cnv)
    {
    }

    void pushPathState(Connection* connection, t_symbol* newPathState)
    {
        connectionUpdateQueue.enqueue({ connection, newPathState });
        startTimer(50);
    }
};
