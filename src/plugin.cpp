#include "plugin.hpp"
#include "model/Scale.hpp"
#include "ui/Theme.hpp"

rack::plugin::Plugin* pluginInstance;

void init(rack::plugin::Plugin* p) {
    pluginInstance = p;
    p->addModel(modelTessera);
    p->addModel(modelTrackOut);
    p->addModel(modelDrumOut);
    p->addModel(modelAutoOut);
}

json_t* settingsToJson() {
    json_t* root = json_object();
    json_object_set_new(root, "colourScheme", json_string(tessera::theme::schemeId(tessera::theme::scheme())));
    json_object_set_new(root, "flats", json_boolean(tessera::accidentals() == tessera::Accidentals::Flats));
    return root;
}

void settingsFromJson(json_t* root) {
    using tessera::theme::Scheme;
    tessera::setAccidentals(json_is_true(json_object_get(root, "flats")) ? tessera::Accidentals::Flats
                                                                         : tessera::Accidentals::Sharps);
    json_t* scheme = json_object_get(root, "colourScheme");
    if (const char* id = json_string_value(scheme)) {
        tessera::theme::setScheme(tessera::theme::schemeFromId(id));
    } else if (json_is_integer(scheme)) {
        // Saved before 13 Oct 2026 as a position in the menu, when the dark schemes were Slate and Walnut.
        // Walnut is gone; Gruvbox is the warm dark scheme now.
        constexpr Scheme kOld[] = {Scheme::FollowRack, Scheme::Plaster, Scheme::Sand, Scheme::Slate, Scheme::Gruvbox};
        json_int_t value = json_integer_value(scheme);
        if (value >= 0 && value < json_int_t(std::size(kOld)))
            tessera::theme::setScheme(kOld[value]);
    }
}
