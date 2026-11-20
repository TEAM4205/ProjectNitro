#pragma once
#include <array>
#include <string>
#include <vector>

class NativeMenuManager {
public:
    enum class ItemType { Text, Action, Switch, Toggle, Blank };

    struct Item {
        ItemType type = ItemType::Blank;
        std::string text;
        std::vector<std::string> options;
        int index = 0;                    
        bool value = false; 

        static Item text_item(std::string t)                  { return {ItemType::Text, std::move(t)}; }
        static Item action_item(std::string t)               { return {ItemType::Action, std::move(t)}; }
        static Item blank_item()                             { return {}; }
        static Item toggle_item(std::string t, bool v)       { Item i{ItemType::Toggle, std::move(t)}; i.value = v; return i; }
        static Item switch_item(std::string t, std::vector<std::string> o, int idx = 0) {
            Item i{ItemType::Switch, std::move(t)}; i.options = std::move(o); i.index = idx; return i;
        }
    };

    struct MenuList { std::vector<Item> items; };

    struct Page { std::string meta, title; std::array<std::string, 3> desc; };
    struct Pages { std::vector<Page> pages; };

    struct Response {
        int selected = -1;
        std::vector<Item> items; 
    };
    static void show(MenuList menu);
    static void show(Pages menu);

    static bool is_menu_busy();
    static void wait_menu_response();
    static Response get_last_menu_response();
};
