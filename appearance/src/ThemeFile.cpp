#include "daw/appearance/ThemeFile.h"

#include <initializer_list>
#include <map>
#include <utility>

namespace daw::appearance
{

namespace
{
namespace json
{
constexpr const char* id = "id";
constexpr const char* name = "name";
constexpr const char* palette = "palette";
constexpr const char* colours = "colours";
constexpr const char* clips = "clips";
constexpr const char* fonts = "fonts";
constexpr const char* shape = "shape";
constexpr const char* meter = "meter";
constexpr const char* effects = "effects";
constexpr const char* summary = "summary";
} // namespace json

template <typename Enum>
using Choices = std::initializer_list<std::pair<const char*, Enum>>;

// Reads one theme, collecting every problem rather than stopping at the first, so a
// theme author sees them all at once.
class Reader
{
public:
    juce::StringArray errors;

    void error(const juce::String& path, const juce::String& message)
    {
        errors.add(path + ": " + message);
    }

    // The member, or a void var after reporting it missing.
    juce::var member(const juce::var& object, const juce::String& path, const char* key)
    {
        if (object.getDynamicObject() == nullptr || !object.hasProperty(key))
        {
            error(join(path, key), "missing");
            return {};
        }

        return object[key];
    }

    bool isObject(const juce::var& value, const juce::String& path)
    {
        if (value.getDynamicObject() != nullptr)
            return true;

        if (!value.isVoid())
            error(path, "must be an object");
        return false;
    }

    void rejectUnknownKeys(const juce::var& object, const juce::String& path,
                           const juce::StringArray& allowed)
    {
        if (auto* dynamicObject = object.getDynamicObject())
            for (const auto& property : dynamicObject->getProperties())
                if (!allowed.contains(property.name.toString()))
                    error(join(path, property.name.toString()), "unknown field");
    }

    std::optional<juce::String> readString(const juce::var& object, const juce::String& path,
                                           const char* key)
    {
        const auto value = member(object, path, key);

        if (value.isVoid())
            return std::nullopt;

        if (!value.isString() || value.toString().trim().isEmpty())
        {
            error(join(path, key), "must be a non-empty string");
            return std::nullopt;
        }

        return value.toString();
    }

    std::optional<float> readNumber(const juce::var& value, const juce::String& path,
                                    double minimum, double maximum)
    {
        if (value.isVoid())
            return std::nullopt;

        if (!(value.isInt() || value.isInt64() || value.isDouble()))
        {
            error(path, "must be a number");
            return std::nullopt;
        }

        const auto number = static_cast<double>(value);

        if (number < minimum || number > maximum)
        {
            error(path, "must be from " + juce::String(minimum) + " to " + juce::String(maximum));
            return std::nullopt;
        }

        return static_cast<float>(number);
    }

    std::optional<float> readNumber(const juce::var& object, const juce::String& path,
                                    const char* key, double minimum, double maximum)
    {
        return readNumber(member(object, path, key), join(path, key), minimum, maximum);
    }

    std::optional<bool> readBool(const juce::var& object, const juce::String& path, const char* key)
    {
        const auto value = member(object, path, key);

        if (value.isVoid())
            return std::nullopt;

        if (!value.isBool())
        {
            error(join(path, key), "must be true or false");
            return std::nullopt;
        }

        return static_cast<bool>(value);
    }

    template <typename Enum>
    std::optional<Enum> readChoice(const juce::var& object, const juce::String& path,
                                   const char* key, Choices<Enum> choices)
    {
        const auto value = member(object, path, key);

        if (value.isVoid())
            return std::nullopt;

        juce::StringArray names;

        for (const auto& [choiceName, choice] : choices)
        {
            if (value.isString() && value.toString() == choiceName)
                return choice;
            names.add(juce::String("\"") + choiceName + "\"");
        }

        error(join(path, key), "must be one of " + names.joinIntoString(", "));
        return std::nullopt;
    }

    // Palette entries are literal colours; other colours may refer to them.
    void readPalette(const juce::var& object)
    {
        if (!isObject(object, json::palette))
            return;

        for (const auto& property : object.getDynamicObject()->getProperties())
        {
            const auto path = join(json::palette, property.name.toString());
            const auto parsed = property.value.isString()
                                    ? Colour::fromHex(property.value.toString().toStdString())
                                    : std::nullopt;

            if (parsed)
                palette[property.name.toString()] = *parsed;
            else
                error(path, "must be a colour, \"#RRGGBB\" or \"#RRGGBBAA\"");
        }
    }

    std::optional<Colour> readColour(const juce::var& value, const juce::String& path)
    {
        if (value.isVoid())
            return std::nullopt;

        if (value.isString())
        {
            const auto text = value.toString();

            if (text.startsWithChar('#'))
            {
                if (const auto parsed = Colour::fromHex(text.toStdString()))
                    return parsed;

                error(path, "\"" + text + "\" is not a colour, \"#RRGGBB\" or \"#RRGGBBAA\"");
                return std::nullopt;
            }

            if (const auto found = palette.find(text); found != palette.end())
                return found->second;

            error(path, "\"" + text + "\" is not in the palette");
            return std::nullopt;
        }

        if (value.getDynamicObject() != nullptr && value.hasProperty("mix"))
        {
            const auto* operands = value["mix"].getArray();

            if (operands == nullptr || operands->size() != 3)
            {
                error(path, "\"mix\" takes [colour, colour, amount]");
                return std::nullopt;
            }

            const auto from = readColour((*operands)[0], path + ".mix[0]");
            const auto to = readColour((*operands)[1], path + ".mix[1]");
            const auto amount = readNumber((*operands)[2], path + ".mix[2]", 0.0, 1.0);

            if (!from || !to || !amount)
                return std::nullopt;

            return from->mixedWith(*to, *amount);
        }

        if (value.getDynamicObject() != nullptr && value.hasProperty("alpha"))
        {
            const auto* operands = value["alpha"].getArray();

            if (operands == nullptr || operands->size() != 2)
            {
                error(path, "\"alpha\" takes [colour, alpha]");
                return std::nullopt;
            }

            const auto colour = readColour((*operands)[0], path + ".alpha[0]");
            const auto alpha = readNumber((*operands)[1], path + ".alpha[1]", 0.0, 1.0);

            if (!colour || !alpha)
                return std::nullopt;

            return colour->withAlpha(*alpha);
        }

        error(path, "must be a palette name, \"#RRGGBB\", {\"mix\": ...} or {\"alpha\": ...}");
        return std::nullopt;
    }

    static juce::String join(const juce::String& path, const juce::String& key)
    {
        return path.isEmpty() ? key : path + "." + key;
    }

private:
    std::map<juce::String, Colour> palette;
};

void readColours(Reader& reader, const juce::var& object, Theme& theme)
{
    if (!reader.isObject(object, json::colours))
        return;

    juce::StringArray keys;

    for (std::size_t index = 0; index < colourRoleCount; ++index)
    {
        const auto key = juce::String(std::string(keyOf(static_cast<ColourRole>(index))));
        keys.add(key);

        const auto value = reader.member(object, json::colours, key.toRawUTF8());
        if (const auto colour = reader.readColour(value, Reader::join(json::colours, key)))
            theme.colours[index] = *colour;
    }

    reader.rejectUnknownKeys(object, json::colours, keys);
}

void readClips(Reader& reader, const juce::var& object, Theme& theme)
{
    if (!reader.isObject(object, json::clips))
        return;

    const juce::String path = json::clips;
    reader.rejectUnknownKeys(object, path, {"colours", "bodyAlpha", "headerAlpha"});

    const auto colours = reader.member(object, path, "colours");
    if (const auto* list = colours.getArray(); list != nullptr && !list->isEmpty())
    {
        for (int index = 0; index < list->size(); ++index)
            if (const auto colour = reader.readColour(
                    (*list)[index], path + ".colours[" + juce::String(index) + "]"))
                theme.clips.colours.push_back(*colour);
    }
    else if (!colours.isVoid())
    {
        reader.error(path + ".colours", "must be a list of at least one colour");
    }

    theme.clips.bodyAlpha = reader.readNumber(object, path, "bodyAlpha", 0.0, 1.0).value_or(0.0f);
    theme.clips.headerAlpha =
        reader.readNumber(object, path, "headerAlpha", 0.0, 1.0).value_or(0.0f);
}

void readFonts(Reader& reader, const juce::var& object, Theme& theme)
{
    if (!reader.isObject(object, json::fonts))
        return;

    juce::StringArray keys;

    for (std::size_t index = 0; index < fontRoleCount; ++index)
    {
        const auto key = juce::String(std::string(keyOf(static_cast<FontRole>(index))));
        keys.add(key);

        const auto spec = reader.member(object, json::fonts, key.toRawUTF8());
        const auto path = Reader::join(json::fonts, key);

        if (!reader.isObject(spec, path))
            continue;

        reader.rejectUnknownKeys(spec, path, {"family", "style", "size", "tracking", "uppercase"});

        auto& font = theme.fonts[index];
        font.family = reader.readString(spec, path, "family").value_or(juce::String());
        font.style = reader.readString(spec, path, "style").value_or(juce::String());
        font.size = reader.readNumber(spec, path, "size", 6.0, 72.0).value_or(0.0f);
        font.tracking = reader.readNumber(spec, path, "tracking", -0.2, 1.0).value_or(0.0f);
        font.uppercase = reader.readBool(spec, path, "uppercase").value_or(false);
    }

    reader.rejectUnknownKeys(object, json::fonts, keys);
}

void readShape(Reader& reader, const juce::var& object, Theme& theme)
{
    if (!reader.isObject(object, json::shape))
        return;

    const juce::String path = json::shape;
    reader.rejectUnknownKeys(object, path,
                             {"corners", "cornerSmall", "cornerMedium", "cornerLarge", "border",
                              "focus", "glow", "labels", "statusBar", "pan"});

    auto& shape = theme.shape;
    shape.corners = reader
                        .readChoice<CornerStyle>(object, path, "corners",
                                                 {{"rounded", CornerStyle::rounded},
                                                  {"sharp", CornerStyle::sharp},
                                                  {"cut", CornerStyle::cut}})
                        .value_or(CornerStyle::rounded);
    shape.cornerSmall = reader.readNumber(object, path, "cornerSmall", 0.0, 24.0).value_or(0.0f);
    shape.cornerMedium = reader.readNumber(object, path, "cornerMedium", 0.0, 24.0).value_or(0.0f);
    shape.cornerLarge = reader.readNumber(object, path, "cornerLarge", 0.0, 24.0).value_or(0.0f);
    shape.border = reader.readNumber(object, path, "border", 0.5, 8.0).value_or(1.0f);
    shape.focus = reader.readNumber(object, path, "focus", 0.5, 8.0).value_or(2.0f);
    shape.glow = reader.readNumber(object, path, "glow", 0.0, 48.0).value_or(0.0f);
    shape.labels = reader
                       .readChoice<LabelStyle>(
                           object, path, "labels",
                           {{"plain", LabelStyle::plain}, {"terminal", LabelStyle::terminal}})
                       .value_or(LabelStyle::plain);
    shape.statusBar = reader
                          .readChoice<StatusBarStyle>(object, path, "statusBar",
                                                      {{"plain", StatusBarStyle::plain},
                                                       {"prompt", StatusBarStyle::prompt}})
                          .value_or(StatusBarStyle::plain);
    shape.pan = reader
                    .readChoice<PanStyle>(object, path, "pan",
                                          {{"knob", PanStyle::knob}, {"arc", PanStyle::arc}})
                    .value_or(PanStyle::knob);

    if (shape.corners == CornerStyle::sharp &&
        (shape.cornerSmall != 0.0f || shape.cornerMedium != 0.0f || shape.cornerLarge != 0.0f))
        reader.error(path, "sharp corners have corner sizes of 0");
}

void readMeter(Reader& reader, const juce::var& object, Theme& theme)
{
    if (!reader.isObject(object, json::meter))
        return;

    const juce::String path = json::meter;
    auto& meter = theme.meter;
    meter.style = reader
                      .readChoice<MeterStyle>(
                          object, path, "style",
                          {{"gradient", MeterStyle::gradient}, {"segments", MeterStyle::segments}})
                      .value_or(MeterStyle::gradient);

    if (meter.style == MeterStyle::segments)
    {
        reader.rejectUnknownKeys(object, path, {"style", "stops", "segmentHeight", "segmentGap"});
        meter.segmentHeight =
            reader.readNumber(object, path, "segmentHeight", 1.0, 16.0).value_or(0.0f);
        meter.segmentGap = reader.readNumber(object, path, "segmentGap", 0.0, 8.0).value_or(0.0f);
    }
    else
    {
        reader.rejectUnknownKeys(object, path, {"style", "stops"});
    }

    const auto stops = reader.member(object, path, "stops");
    const auto* list = stops.getArray();

    if (list == nullptr || list->size() < 2)
    {
        if (!stops.isVoid())
            reader.error(path + ".stops", "must be a list of at least two stops");
        return;
    }

    for (int index = 0; index < list->size(); ++index)
    {
        const auto stopPath = path + ".stops[" + juce::String(index) + "]";
        const auto& stop = (*list)[index];

        if (!reader.isObject(stop, stopPath))
            continue;

        reader.rejectUnknownKeys(stop, stopPath, {"db", "colour"});
        const auto decibels = reader.readNumber(stop, stopPath, "db", -120.0, 0.0);
        const auto colour =
            reader.readColour(reader.member(stop, stopPath, "colour"), stopPath + ".colour");

        if (!decibels || !colour)
            continue;

        if (!meter.stops.empty() && *decibels >= meter.stops.back().decibels)
            reader.error(stopPath, "stops must run from the loudest down");

        meter.stops.push_back({*decibels, *colour});
    }
}

void readEffects(Reader& reader, const juce::var& object, Theme& theme)
{
    if (!reader.isObject(object, json::effects))
        return;

    juce::StringArray keys;

    for (const auto animationEffect : allAnimationEffects())
    {
        const auto key = juce::String(std::string(keyOf(animationEffect)));
        keys.add(key);

        const auto spec = reader.member(object, json::effects, key.toRawUTF8());
        const auto path = Reader::join(json::effects, key);

        if (!reader.isObject(spec, path))
            continue;

        reader.rejectUnknownKeys(spec, path, {"available", "cost", "description"});

        auto& look = theme.effects[static_cast<std::size_t>(animationEffect)];
        look.available = reader.readBool(spec, path, "available").value_or(false);

        // An effect the theme does not draw needs no cost or description.
        if (!look.available && !spec.hasProperty("cost") && !spec.hasProperty("description"))
            continue;

        look.cost = reader
                        .readChoice<Cost>(
                            spec, path, "cost",
                            {{"low", Cost::low}, {"medium", Cost::medium}, {"high", Cost::high}})
                        .value_or(Cost::low);
        look.description = reader.readString(spec, path, "description").value_or(juce::String());
    }

    reader.rejectUnknownKeys(object, json::effects, keys);
}
} // namespace

ThemeReadResult readTheme(const juce::String& text)
{
    juce::var root;

    if (const auto parsed = juce::JSON::parse(text, root); parsed.failed())
        return {std::nullopt, juce::StringArray("not valid JSON: " + parsed.getErrorMessage())};

    if (root.getDynamicObject() == nullptr)
        return {std::nullopt, juce::StringArray("the theme must be a JSON object")};

    Reader reader;
    Theme theme;

    reader.rejectUnknownKeys(root, {},
                             {json::id, json::name, json::palette, json::colours, json::clips,
                              json::fonts, json::shape, json::meter, json::effects, json::summary});

    theme.id = reader.readString(root, {}, json::id).value_or(juce::String());
    theme.name = reader.readString(root, {}, json::name).value_or(juce::String());

    reader.readPalette(reader.member(root, {}, json::palette));
    readColours(reader, reader.member(root, {}, json::colours), theme);
    readClips(reader, reader.member(root, {}, json::clips), theme);
    readFonts(reader, reader.member(root, {}, json::fonts), theme);
    readShape(reader, reader.member(root, {}, json::shape), theme);
    readMeter(reader, reader.member(root, {}, json::meter), theme);
    readEffects(reader, reader.member(root, {}, json::effects), theme);

    const auto summary = reader.member(root, {}, json::summary);
    if (reader.isObject(summary, json::summary))
    {
        reader.rejectUnknownKeys(summary, json::summary, {"shapes", "meters"});
        theme.shapeSummary =
            reader.readString(summary, json::summary, "shapes").value_or(juce::String());
        theme.meterSummary =
            reader.readString(summary, json::summary, "meters").value_or(juce::String());
    }

    if (!reader.errors.isEmpty())
        return {std::nullopt, reader.errors};

    return {std::move(theme), {}};
}

} // namespace daw::appearance
