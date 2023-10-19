/*
 // Copyright (c) 2021-2022 Timothy Schoen
 // For information on usage and redistribution, and for a DISCLAIMER OF ALL
 // WARRANTIES, see the file, "LICENSE.txt," in this distribution.
 */

extern "C" {
void garray_arraydialog(t_fake_garray* x, t_symbol* name, t_floatarg fsize, t_floatarg fflags, t_floatarg deleteit);
}

class GraphicalArray : public Component
    , public pd::MessageListener {
public:
    Object* object;

    enum DrawType {
        Points,
        Polygon,
        Curve
    };

    GraphicalArray(PluginProcessor* instance, void* ptr, Object* parent)
        : object(parent)
        , arr(ptr, instance)
        , edited(false)
        , pd(instance)
    {
        vec.reserve(8192);
        temp.reserve(8192);
        try {
            read(vec);
        } catch (...) {
            error = true;
        }

        setInterceptsMouseClicks(true, false);
        setOpaque(false);

        MessageManager::callAsync([this] {
            object->getConstrainer()->setMinimumSize(100 - Object::doubleMargin, 40 - Object::doubleMargin);
        });
    }

    void receiveMessage(String const& name, int argc, t_atom* argv) override
    {
        std::cout << name << std::endl;
    }

    void setArray(void* array)
    {
        if (!array)
            return;

        // TODO: Not great design

        // Manually call destructor
        arr.~WeakReference();

        // Initialise new weakreference in place, to prevent calling copy constructor
        new (&arr) pd::WeakReference(array, pd);
    }

    std::vector<float> rescale(std::vector<float> const& v, unsigned const newSize)
    {
        if (v.empty()) {
            return {};
        }

        std::vector<float> result(newSize);
        const std::size_t oldSize = v.size();
        for (unsigned i = 0; i < newSize; i++) {
            auto const idx = i * (oldSize - 1) / newSize;
            auto const mod = i * (oldSize - 1) % newSize;

            if (mod == 0)
                result[i] = v[idx];
            else {
                float const part = float(mod) / float(newSize);
                result[i] = v[idx] * (1.0 - part) + v[idx + 1] * part;
            }
        }
        return result;
    }

    void paintGraph(Graphics& g)
    {
        auto const h = static_cast<float>(getHeight());
        auto const w = static_cast<float>(getWidth());
        std::vector<float> points = vec;

        if (!points.empty()) {
            std::array<float, 2> scale = getScale();
            bool invert = false;

            if (scale[0] >= scale[1]) {
                invert = true;
                std::swap(scale[0], scale[1]);
            }

            // More than a point per pixel will cause insane loads, and isn't actually helpful
            // Instead, linearly interpolate the vector to a max size of width in pixels
            if (vec.size() >= w) {
                points = rescale(points, w);
            }

            float const dh = h / (scale[1] - scale[0]);
            float const dw = w / static_cast<float>(points.size() - 1);

            switch (getDrawType()) {
            case DrawType::Curve: {

                Path p;
                p.startNewSubPath(0, h - (std::clamp(points[0], scale[0], scale[1]) - scale[0]) * dh);

                for (size_t i = 1; i < points.size() - 2; i += 3) {
                    float const y1 = h - (std::clamp(points[i], scale[0], scale[1]) - scale[0]) * dh;
                    float const y2 = h - (std::clamp(points[i + 1], scale[0], scale[1]) - scale[0]) * dh;
                    float const y3 = h - (std::clamp(points[i + 2], scale[0], scale[1]) - scale[0]) * dh;
                    p.cubicTo(static_cast<float>(i) * dw, y1, static_cast<float>(i + 1) * dw, y2, static_cast<float>(i + 2) * dw, y3);
                }

                if (invert)
                    p.applyTransform(AffineTransform::verticalFlip(getHeight()));

                g.setColour(getContentColour());
                g.strokePath(p, PathStrokeType(getLineWidth()));
                break;
            }
            case DrawType::Polygon: {
                int startY = h - (std::clamp(points[0], scale[0], scale[1]) - scale[0]) * dh;
                Point<float> lastPoint = Point<float>(0, startY);
                Point<float> newPoint;

                Path p;
                for (size_t i = 1; i < points.size(); i++) {
                    float const y = h - (std::clamp(points[i], scale[0], scale[1]) - scale[0]) * dh;
                    newPoint = Point<float>(static_cast<float>(i) * dw, y);

                    p.addLineSegment({ lastPoint, newPoint }, getLineWidth());
                    lastPoint = newPoint;
                }

                if (invert)
                    p.applyTransform(AffineTransform::verticalFlip(getHeight()));

                g.setColour(getContentColour());
                g.fillPath(p);
                break;
            }
            case DrawType::Points: {
                g.setColour(getContentColour());

                float const dw_points = w / static_cast<float>(points.size());

                for (size_t i = 0; i < points.size(); i++) {
                    float y = h - (std::clamp(points[i], scale[0], scale[1]) - scale[0]) * dh;
                    if (invert)
                        y = getHeight() - y;
                    g.drawLine(static_cast<float>(i) * dw_points, y, static_cast<float>(i + 1) * dw_points, y, getLineWidth());
                }
                break;
            }
            default:
                break;
            }
        }
    }

    void paint(Graphics& g) override
    {
        if (error) {
            // TODO: error colour
            Fonts::drawText(g, "array " + getUnexpandedName() + " is invalid", 0, 0, getWidth(), getHeight(), object->findColour(PlugDataColour::canvasTextColourId), 15, Justification::centred);
            error = false;
        } else {
            paintGraph(g);
        }
    }

    void mouseDown(MouseEvent const& e) override
    {
        if (error || !getEditMode())
            return;
        edited = true;

        auto const s = static_cast<float>(vec.size() - 1);
        auto const w = static_cast<float>(getWidth());
        auto const x = static_cast<float>(e.x);

        lastIndex = std::round(std::clamp(x / w, 0.f, 1.f) * s);

        mouseDrag(e);
    }

    void mouseDrag(MouseEvent const& e) override
    {
        if (error || !getEditMode())
            return;

        auto const s = static_cast<float>(vec.size() - 1);
        auto const w = static_cast<float>(getWidth());
        auto const h = static_cast<float>(getHeight());
        auto const x = static_cast<float>(e.x);
        auto const y = static_cast<float>(e.y);

        std::array<float, 2> scale = getScale();

        int const index = static_cast<int>(std::round(std::clamp(x / w, 0.f, 1.f) * s));

        float start = vec[lastIndex];
        float current = (1.f - std::clamp(y / h, 0.f, 1.f)) * (scale[1] - scale[0]) + scale[0];

        int interpStart = std::min(index, lastIndex);
        int interpEnd = std::max(index, lastIndex);

        float min = index == interpStart ? current : start;
        float max = index == interpStart ? start : current;

        // Fix to make sure we don't leave any gaps while dragging
        for (int n = interpStart; n <= interpEnd; n++) {
            vec[n] = jmap<float>(n, interpStart, interpEnd + 1, min, max);
        }

        // Don't want to touch vec on the other thread, so we copy the vector into the lambda
        auto changed = std::vector<float>(vec.begin() + interpStart, vec.begin() + interpEnd + 1);

        lastIndex = index;

        pd->lockAudioThread();
        for (int n = 0; n < changed.size(); n++) {
            write(interpStart + n, changed[n]);
        }

        if (auto ptr = arr.get<t_garray>()) {
            pd->sendDirectMessage(ptr.get(), stringArray);
        }

        pd->unlockAudioThread();
        repaint();
    }

    void mouseUp(MouseEvent const& e) override
    {
        if (error || !getEditMode())
            return;
        edited = false;
    }

    void update()
    {
        // Check if size has changed
        int currentSize = size();
        if (vec.size() != currentSize) {
            vec.resize(currentSize);
        }

        if (!edited) {
            error = false;
            try {
                read(temp);
            } catch (...) {
                error = true;
            }
            if (temp != vec) {
                vec.swap(temp);
                repaint();
            }
        }
    }

    bool willSaveContent() const
    {
        if (auto ptr = arr.get<t_fake_garray>()) {
            return ptr->x_saveit;
        }

        return false;
    }

    // Gets the name of the array. Currently not used anywhere
    String getExpandedName() const
    {
        if (auto ptr = arr.get<t_fake_garray>()) {
            return String::fromUTF8(ptr->x_realname->s_name);
        }

        return {};
    }

    // Gets the text label of the array
    String getUnexpandedName() const
    {
        if (auto ptr = arr.get<t_fake_garray>()) {
            return String::fromUTF8(ptr->x_name->s_name);
        }

        return {};
    }

    int getLineWidth()
    {
        if (auto ptr = arr.get<t_fake_garray>()) {
            if (ptr->x_scalar) {
                t_scalar* scalar = ptr->x_scalar;
                t_template* scalartplte = template_findbyname(scalar->sc_template);
                if (scalartplte) {
                    int result = (int)template_getfloat(scalartplte, gensym("linewidth"), scalar->sc_vec, 1);
                    return result;
                }
            }
        }

        return 1;
    }
    DrawType getDrawType() const
    {
        if (auto ptr = arr.get<t_fake_garray>()) {
            if (ptr->x_scalar) {
                t_scalar* scalar = ptr->x_scalar;
                t_template* scalartplte = template_findbyname(scalar->sc_template);
                if (scalartplte) {
                    int result = (int)template_getfloat(scalartplte, gensym("style"), scalar->sc_vec, 0);
                    return static_cast<DrawType>(result);
                }
            }
        }

        return DrawType::Points;
    }

    // Gets the scale of the array
    std::array<float, 2> getScale() const
    {
        if (auto ptr = arr.get<t_fake_garray>()) {
            t_canvas const* cnv = ptr->x_glist;
            if (cnv) {
                float min = cnv->gl_y2;
                float max = cnv->gl_y1;

                if (approximatelyEqual(min, max))
                    max += 1e-6;

                return { min, max };
            }
        }

        return { -1.0f, 1.0f };
    }

    bool getEditMode() const
    {
        if (auto ptr = arr.get<t_fake_garray>()) {
            return ptr->x_edit;
        }

        return true;
    }

    void setEditMode(bool editMode)
    {
        if (auto ptr = arr.get<t_fake_garray>()) {
            ptr->x_edit = editMode;
        }
    }

    // Gets the scale of the array.
    int size() const
    {
        if (auto ptr = arr.get<t_garray>()) {
            return garray_getarray(ptr.get())->a_n;
        }

        return 0;
    }

    Colour getContentColour()
    {
        if (auto garray = arr.get<t_fake_garray>()) {
            auto* scalar = garray->x_scalar;
            auto* templ = template_findbyname(scalar->sc_template);

            int colour = template_getfloat(templ, gensym("color"), scalar->sc_vec, 1);

            if (colour <= 0) {
                return object->findColour(PlugDataColour::guiObjectInternalOutlineColour);
            }

            auto rangecolor = [](int n) /* 0 to 9 in 5 steps */
            {
                int n2 = (n == 9 ? 8 : n); /* 0 to 8 */
                int ret = (n2 << 5);       /* 0 to 256 in 9 steps */
                if (ret > 255)
                    ret = 255;
                return (ret);
            };

            int red = rangecolor(colour / 100);
            int green = rangecolor((colour / 10) % 10);
            int blue = rangecolor(colour % 10);

            return Colour(red, green, blue);
        }

        return object->findColour(PlugDataColour::guiObjectInternalOutlineColour);
    }

    void setScale(std::array<float, 2> scale)
    {
        auto& [min, max] = scale;
        if (auto ptr = arr.get<t_fake_garray>()) {
            t_canvas* cnv = ptr->x_glist;
            if (cnv) {
                cnv->gl_y2 = min;
                cnv->gl_y1 = max;
                return;
            }
        }
    }

    // Gets the values from the array.
    void read(std::vector<float>& output) const
    {
        if (auto ptr = arr.get<t_garray>()) {
            int const size = garray_getarray(ptr.get())->a_n;
            output.resize(static_cast<size_t>(size));

            t_word* vec = ((t_word*)garray_vec(ptr.get()));
            for (int i = 0; i < size; i++)
                output[i] = vec[i].w_float;
        }
    }

    // Writes a value to the array.
    void write(const size_t pos, float const input)
    {
        if (auto ptr = arr.get<t_garray>()) {
            t_word* vec = ((t_word*)garray_vec(ptr.get()));
            vec[pos].w_float = input;
        }
    }

    pd::WeakReference arr;

    std::vector<float> vec;
    std::vector<float> temp;
    std::atomic<bool> edited;
    bool error = false;
    const String stringArray = "array";

    int lastIndex = 0;

    PluginProcessor* pd;
};

class ArrayEditorDialog : public Component {
    ResizableBorderComponent resizer;
    std::unique_ptr<Button> closeButton;
    ComponentDragger windowDragger;
    ComponentBoundsConstrainer constrainer;

public:
    std::function<void()> onClose;
    OwnedArray<GraphicalArray> graphs;
    PluginProcessor* pd;
    String title;

    ArrayEditorDialog(PluginProcessor* instance, std::vector<void*> arrays, Object* parent)
        : resizer(this, &constrainer)
        , pd(instance)
    {
        for (auto* arr : arrays) {
            auto* graph = graphs.add(new GraphicalArray(pd, arr, parent));
            addAndMakeVisible(graph);
        }

        title = graphs[0]->getUnexpandedName();

        closeButton.reset(LookAndFeel::getDefaultLookAndFeel().createDocumentWindowButton(DocumentWindow::closeButton));
        addAndMakeVisible(closeButton.get());

        constrainer.setMinimumSize(500, 200);

        closeButton->onClick = [this]() {
            MessageManager::callAsync([this]() {
                onClose();
            });
        };

        addToDesktop(ComponentPeer::windowIsTemporary | ComponentPeer::windowHasDropShadow);
        setVisible(true);

        // Position in centre of screen
        setBounds(Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea.withSizeKeepingCentre(600, 400));

        addAndMakeVisible(resizer);
        updateGraphs();
    }

    void resized() override
    {
        resizer.setBounds(getLocalBounds());

        auto macOSStyle = SettingsFile::getInstance()->getProperty<bool>("macos_buttons");
        auto closeButtonBounds = getLocalBounds().removeFromTop(30).removeFromRight(30).translated(-5, 5);
        closeButton->setBounds(closeButtonBounds.reduced(macOSStyle ? 5 : 0));

        for (auto* graph : graphs) {
            graph->setBounds(getLocalBounds().withTrimmedTop(40));
        }
    }

    void updateGraphs()
    {
        if (!pd->tryLockAudioThread())
            return;

        for (auto* graph : graphs) {
            graph->update();
        }

        pd->unlockAudioThread();
    }

    void mouseDown(MouseEvent const& e) override
    {
        windowDragger.startDraggingComponent(this, e);
    }

    void mouseDrag(MouseEvent const& e) override
    {
        windowDragger.dragComponent(this, e, nullptr);
    }

    void paintOverChildren(Graphics& g) override
    {
        g.setColour(findColour(PlugDataColour::guiObjectBackgroundColourId));
        g.drawRoundedRectangle(getLocalBounds().toFloat(), Corners::windowCornerRadius, 1.0f);
    }

    void paint(Graphics& g) override
    {
        g.setColour(findColour(PlugDataColour::guiObjectBackgroundColourId));
        g.fillRoundedRectangle(getLocalBounds().toFloat(), Corners::windowCornerRadius);

        g.setColour(findColour(PlugDataColour::canvasTextColourId));
        g.drawHorizontalLine(39, 0, getWidth());

        if (!title.isEmpty()) {
            Fonts::drawText(g, title, 0, 0, getWidth(), 40, findColour(PlugDataColour::canvasTextColourId), 15, Justification::centred);
        }
    }
};

class ArrayObject final : public ObjectBase {
public:
    // Array component
    ArrayObject(t_gobj* obj, Object* object)
        : ObjectBase(obj, object)
    {
        auto arrays = getArrays();

        for (int i = 0; i < arrays.size(); i++) {
            auto* graph = graphs.add(new GraphicalArray(cnv->pd, arrays[i], object));
            graph->setBounds(getLocalBounds());
            addAndMakeVisible(graph);
        }

        setInterceptsMouseClicks(false, true);

        objectParameters.addParamSize(&sizeProperty);
        objectParameters.addParamString("Name", cGeneral, &name);
        objectParameters.addParamInt("Size", cGeneral, &size);

        objectParameters.addParamRange("Y range", cGeneral, &range, { -1.0f, 1.0f });
        objectParameters.addParamBool("Save contents", cGeneral, &saveContents, { "No", "Yes" }, 0);

        objectParameters.addParamCombo("Draw mode", cAppearance, &drawMode, { "Points", "Polygon", "Bezier Curve" }, 2);

        updateGraphs();
    }

    void updateGraphs()
    {
        pd->lockAudioThread();

        for (auto* graph : graphs) {
            // Update values
            graph->update();
        }

        size = static_cast<int>(graphs[0]->vec.size());

        pd->unlockAudioThread();
    }

    void updateLabel() override
    {
        int fontHeight = 14.0f;

        const String text = graphs[0]->getUnexpandedName();

        if (text.isNotEmpty()) {
            if (!label) {
                label = std::make_unique<ObjectLabel>();
            }

            auto bounds = object->getBounds().reduced(Object::margin).removeFromTop(fontHeight + 2);

            bounds.translate(2, -(fontHeight + 2));

            label->setFont(Font(fontHeight));
            label->setBounds(bounds);
            label->setText(text, dontSendNotification);

            label->setColour(Label::textColourId, object->findColour(PlugDataColour::canvasTextColourId));

            object->cnv->addAndMakeVisible(label.get());
        }
    }

    Rectangle<int> getPdBounds() override
    {
        if (auto glist = ptr.get<_glist>()) {

            auto* patch = cnv->patch.getPointer().get();
            if (!patch)
                return {};

            int x = 0, y = 0, w = 0, h = 0;
            pd::Interface::getObjectBounds(patch, &glist->gl_obj.te_g, &x, &y, &w, &h);

            return { x, y, glist->gl_pixwidth, glist->gl_pixheight };
        }

        return {};
    }

    void setPdBounds(Rectangle<int> b) override
    {
        if (auto glist = ptr.get<t_glist>()) {
            auto* patch = cnv->patch.getPointer().get();
            if (!patch)
                return;

            pd::Interface::moveObject(patch, glist.cast<t_gobj>(), b.getX(), b.getY());

            glist->gl_pixwidth = b.getWidth();
            glist->gl_pixheight = b.getHeight();
        }
    }

    void resized() override
    {
        for (auto* graph : graphs) {
            graph->setBounds(getLocalBounds());
        }
    }

    void update() override
    {
        auto scale = graphs[0]->getScale();
        range = var(Array<var> { var(scale[0]), var(scale[1]) });
        size = var(static_cast<int>(graphs[0]->size()));
        saveContents = graphs[0]->willSaveContent();
        name = String(graphs[0]->getUnexpandedName());
        drawMode = static_cast<int>(graphs[0]->getDrawType()) + 1;
        if (auto glist = ptr.get<t_glist>()) {
            sizeProperty = Array<var> { var(glist->gl_pixwidth), var(glist->gl_pixheight) };
        }

        labelColour = object->findColour(PlugDataColour::canvasTextColourId).toString();
    }

    void updateSizeProperty() override
    {
        setPdBounds(object->getObjectBounds());

        if (auto glist = ptr.get<t_glist>()) {
            setParameterExcludingListener(sizeProperty, Array<var> { var(glist->gl_pixwidth), var(glist->gl_pixheight) });
        }
    }

    void updateSettings()
    {
        auto arrName = name.getValue().toString();
        auto arrSize = std::max(0, getValue<int>(size));
        auto arrDrawMode = getValue<int>(drawMode) - 1;

        if (arrSize != getValue<int>(size)) {
            size = arrSize;
        }

        // This flag is swapped for some reason
        if (arrDrawMode == 0) {
            arrDrawMode = 1;
        } else if (arrDrawMode == 1) {
            arrDrawMode = 0;
        }

        auto arrSaveContents = getValue<bool>(saveContents);

        int flags = arrSaveContents + 2 * static_cast<int>(arrDrawMode);

        if (auto arrayCanvas = ptr.get<t_canvas>()) {
            bool first = true;
            for (auto* graph : graphs) {
                t_symbol* name = first ? pd->generateSymbol(arrName) : pd->generateSymbol(graph->getUnexpandedName());
                first = false;

                if (auto garray = graph->arr.get<t_fake_garray>()) {
                    garray_arraydialog(garray.get(), name, arrSize, static_cast<float>(flags), 0.0f);
                }
            }
        }

        auto arrays = getArrays();

        for (int i = 0; i < arrays.size(); i++) {
            graphs[i]->setArray(arrays[i]);
        }

        updateLabel();

        for (auto* graph : graphs) {
            graph->repaint();
        }
    }

    void valueChanged(Value& value) override
    {
        if (value.refersToSameSourceAs(sizeProperty)) {
            auto& arr = *sizeProperty.getValue().getArray();
            auto* constrainer = getConstrainer();
            auto width = std::max(int(arr[0]), constrainer->getMinimumWidth());
            auto height = std::max(int(arr[1]), constrainer->getMinimumHeight());

            setParameterExcludingListener(sizeProperty, Array<var> { var(width), var(height) });

            if (auto glist = ptr.get<t_glist>()) {
                glist->gl_pixwidth = width;
                glist->gl_pixheight = height;
            }

            object->updateBounds();
        } else if (value.refersToSameSourceAs(name) || value.refersToSameSourceAs(size) || value.refersToSameSourceAs(drawMode) || value.refersToSameSourceAs(saveContents)) {
            updateSettings();
        } else if (value.refersToSameSourceAs(range)) {
            auto min = static_cast<float>(range.getValue().getArray()->getReference(0));
            auto max = static_cast<float>(range.getValue().getArray()->getReference(1));
            for (auto* graph : graphs) {
                graph->setScale({ min, max });
                graph->repaint();
            }
        } else {
            ObjectBase::valueChanged(value);
        }
    }

    void paint(Graphics& g) override
    {
        g.setColour(object->findColour(PlugDataColour::guiObjectBackgroundColourId));
        g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), Corners::objectCornerRadius);
    }

    void paintOverChildren(Graphics& g) override
    {
        bool selected = object->isSelected() && !cnv->isGraph;
        auto outlineColour = object->findColour(selected ? PlugDataColour::objectSelectedOutlineColourId : objectOutlineColourId);

        g.setColour(outlineColour);
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), Corners::objectCornerRadius, 1.0f);
    }

    std::vector<void*> getArrays() const
    {
        if (auto c = ptr.get<t_canvas>()) {
            std::vector<void*> arrays;

            t_gobj* x = reinterpret_cast<t_gobj*>(c->gl_list);
            arrays.push_back(x);

            while ((x = x->g_next)) {
                arrays.push_back(x);
            }

            return arrays;
        }

        return {};
    }

    bool canOpenFromMenu() override
    {
        return true;
    }

    void openFromMenu() override
    {
        if (dialog) {
            dialog->toFront(true);
            return;
        }

        dialog = std::make_unique<ArrayEditorDialog>(cnv->pd, getArrays(), object);
        dialog->onClose = [this]() {
            dialog.reset(nullptr);
        };
    }

    std::vector<hash32> getAllMessages() override
    {
        return {
            hash("float"),
            hash("symbol"),
            hash("list"),
            hash("edit"),
            hash("width"),
            hash("rename"),
            hash("color"),
            hash("style"),
            hash("redraw"),
        };
    }

    void receiveObjectMessage(String const& symbol, std::vector<pd::Atom>& atoms) override
    {
        switch (hash(symbol)) {
        case hash("float"):
        case hash("symbol"):
        case hash("list"): {
            break;
        }
        case hash("redraw"): {
            updateGraphs();
            if (dialog) {
                dialog->updateGraphs();
            }
            break;
        }
        case hash("edit"): {
            if (!atoms.empty()) {
                editable = atoms[0].getFloat();
                setInterceptsMouseClicks(false, editable);
            }
        }
        case hash("rename"): {
            // When we receive a rename message, recreate the array object
            MessageManager::callAsync([_this = SafePointer(this)]() {
                if (!_this)
                    return;

                _this->cnv->setSelected(_this->object, false);
                _this->object->cnv->editor->sidebar->hideParameters();

                _this->object->setType(_this->getText(), _this->ptr.getRaw<t_gobj>());
            });

            break;
        }
        case hash("color"): {
            repaint();
            break;
        }
        case hash("width"): {
            repaint();
            break;
        }
        case hash("style"): {
            drawMode = static_cast<int>(atoms[0].getFloat()) + 1;
            updateSettings();
            break;
        }
        default:
            break;
        }
    }

private:
    Value name = SynchronousValue();
    Value size = SynchronousValue();
    Value drawMode = SynchronousValue();
    Value saveContents = SynchronousValue();
    Value range = SynchronousValue();
    Value sizeProperty = SynchronousValue();

    OwnedArray<GraphicalArray> graphs;
    std::unique_ptr<ArrayEditorDialog> dialog = nullptr;

    Value labelColour = SynchronousValue();
    bool editable = true;
};

class ArrayDefineObject final : public TextBase {
    std::unique_ptr<ArrayEditorDialog> editor = nullptr;

public:
    ArrayDefineObject(t_gobj* obj, Object* parent)
        : TextBase(obj, parent, true)
    {
    }

    void lock(bool isLocked) override
    {
        setInterceptsMouseClicks(isLocked, false);
    }

    void mouseDown(MouseEvent const& e) override
    {
        if (!e.mods.isLeftButtonDown())
            return;

        openArrayEditor();
    }

    bool canOpenFromMenu() override
    {
        if (auto c = ptr.get<t_canvas>()) {
            return c->gl_list != nullptr;
        }

        return false;
    }

    void openArrayEditor()
    {
        if (editor) {
            editor->toFront(true);
            return;
        }

        if (auto c = ptr.get<t_canvas>()) {
            std::vector<void*> arrays;

            t_glist* x = c.get();
            t_gobj* gl = (x->gl_list ? pd_checkglist(&x->gl_list->g_pd)->gl_list : 0);

            if (gl) {
                arrays.push_back(gl);
                while ((gl = gl->g_next)) {
                    arrays.push_back(x);
                }
            }

            if (arrays.size() && arrays[0] != nullptr) {
                editor = std::make_unique<ArrayEditorDialog>(cnv->pd, arrays, object);
                editor->onClose = [this]() {
                    editor.reset(nullptr);
                };
            } else {
                pd->logWarning("array define: cannot open non-existent array");
            }
        }
    }
    std::vector<hash32> getAllMessages() override
    {
        return {
            hash("redraw"),
        };
    }

    void receiveObjectMessage(String const& symbol, std::vector<pd::Atom>& atoms) override
    {
    }
    void openFromMenu() override
    {
        openArrayEditor();
    }
};
