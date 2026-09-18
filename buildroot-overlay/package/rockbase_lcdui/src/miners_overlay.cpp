#include "miners_overlay.h"

#include <algorithm>
#include <cstdio>

namespace {

void style_card(lv_obj_t* card) {
    lv_obj_set_style_radius(card, 18, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x132028), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x2f4d59), 0);
    lv_obj_set_style_pad_all(card, 16, 0);
}

lv_obj_t* add_text(lv_obj_t* parent, const char* text, int32_t x, int32_t y,
                   const lv_font_t* font, uint32_t color) {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text != nullptr ? text : "");
    lv_obj_set_pos(label, x, y);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    return label;
}

std::string metric_line(const char* left, const std::string& value) {
    return std::string(left) + value;
}

std::string compact_address(const std::string& address) {
    if (address.size() <= 19) {
        return address;
    }
    return address.substr(0, 8) + "..." + address.substr(address.size() - 8);
}

}  // namespace

void MinersOverlay::set_snapshot(const Snapshot& snapshot) {
    snapshot_ = snapshot;
    if (overlay_.visible()) {
        rebuild();
    }
}

void MinersOverlay::open() {
    overlay_.show("Connected Miners");
    rebuild();
}

void MinersOverlay::update() {
    if (overlay_.visible() && overlay_.close_requested()) {
        close();
    }
}

void MinersOverlay::close() {
    overlay_.hide();
}

void MinersOverlay::rebuild() {
    lv_obj_t* content = overlay_.content();
    if (content == nullptr) {
        return;
    }

    for (uint32_t i = lv_obj_get_child_count(content); i > 0; --i) {
        lv_obj_t* child = lv_obj_get_child(content, i - 1);
        if (child != nullptr && i > 2) {
            lv_obj_del(child);
        }
    }

    lv_obj_t* list = lv_obj_create(content);
    lv_obj_remove_style_all(list);
    // Start below the ghost close button (which spans y 16-72) so the first
    // list row is not covered by the close hit-target.
    lv_obj_set_pos(list, 24, 80);
    lv_obj_set_size(list, 712, 356);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_style_pad_row(list, 10, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);

    if (snapshot_.miners.empty()) {
        lv_obj_t* empty = lv_label_create(list);
        lv_label_set_text(empty, "No miners connected");
        lv_obj_set_width(empty, 712);
        lv_obj_set_style_text_font(empty, &lv_font_montserrat_22, 0);
        lv_obj_set_style_text_color(empty, lv_color_hex(0xf4efe2), 0);
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);

        lv_obj_t* hint = lv_label_create(list);
        lv_label_set_text(hint, "Connect miners to the local Stratum endpoint.");
        lv_obj_set_width(hint, 712);
        lv_obj_set_style_text_font(hint, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(hint, lv_color_hex(0x8c9aa1), 0);
        lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
        return;
    }

    for (const Miner& miner : snapshot_.miners) {
        lv_obj_t* card = lv_obj_create(list);
        lv_obj_set_size(card, LV_PCT(100), 86);
        style_card(card);

        lv_obj_t* dot = lv_obj_create(card);
        lv_obj_remove_style_all(dot);
        lv_obj_set_pos(dot, 16, 16);
        lv_obj_set_size(dot, 14, 14);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(dot, lv_color_hex(miner.online ? 0x37e88b : 0x68757c), 0);

        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

        const std::string address_text = compact_address(miner.address);
        lv_obj_t* addr = add_text(card, address_text.c_str(), 40, 12, &lv_font_montserrat_22, 0xf4efe2);
        lv_obj_set_width(addr, 330);
        lv_label_set_long_mode(addr, LV_LABEL_LONG_MODE_DOTS);

        lv_obj_t* hash = add_text(card, miner.hashrate_5m.c_str(), 386, 26, &lv_font_montserrat_36, 0xf0b44c);
        lv_obj_set_width(hash, 290);
        lv_obj_set_style_text_align(hash, LV_TEXT_ALIGN_RIGHT, 0);

        const std::string workers = metric_line("Workers ", std::to_string(std::max(0, miner.workers)));
        add_text(card, workers.c_str(), 40, 42, &lv_font_montserrat_16, 0x9fb1b9);

        const std::string last_share = metric_line("Last ", miner.last_share_ago.empty() ? std::string("-") : miner.last_share_ago);
        add_text(card, last_share.c_str(), 154, 42, &lv_font_montserrat_16, 0x9fb1b9);

        const std::string best_share = metric_line("Best ", miner.best_share.empty() ? std::string("--") : miner.best_share);
        add_text(card, best_share.c_str(), 296, 42, &lv_font_montserrat_16, 0x9fb1b9);
    }
}
