// Maps app state into editor-page model/actions and dispatches editor UI calls.
#include "drumrom/main_ui_editor_dispatch.h"

#include "drumrom/ui_editor_page.h"

namespace drumrom::main_ui_editor_dispatch {

void render(const Model& model, State* state, const Actions& actions) {
    if (state == nullptr || state->changed == nullptr) {
        return;
    }

    drumrom::ui_editor_page::Model editor_model{};
    editor_model.ui_scale = model.ui_scale;
    editor_model.waveform_pane_height = model.waveform_pane_height;
    editor_model.status = model.status;
    editor_model.status_expire_time = model.status_expire_time;
    editor_model.status_color = model.status_color;

    drumrom::ui_editor_page::State editor_state{};
    editor_state.slot_config = state->slot_config;
    editor_state.changed = state->changed;

    const drumrom::ui_editor_page::Actions editor_actions{
        actions.render_left_pane,
        actions.render_action_pane,
        actions.render_bottom_toolbar,
    };

    drumrom::ui_editor_page::render(editor_model, &editor_state, editor_actions);
}

}  // namespace drumrom::main_ui_editor_dispatch
