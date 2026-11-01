/*
 savepkg tool for trying the save package code on real saves without the game
 info SAV prints the chunks and the top level nodes. roundtrip IN OUT reads and writes a save and checks the result reads back
 the same. splice HOST GUEST OUT [nodes...] puts the guests character nodes (or the named ones) into the hosts save. treetrip SAV
 turns the save into trees and back and checks nothing moved. inventory HOST GUEST OUT puts the guests player and stash sub
 inventories into the hosts save
*/
#include "net/SavePackage.hpp"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
bool Load(const char* path, nightcoop_mac::save_package::SaveFile& save)
{
    std::ifstream file(path, std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::string error;
    if (!nightcoop_mac::save_package::Read(bytes, save, error))
    {
        std::printf("savepkg: %s: %s\n", path, error.c_str());
        return false;
    }
    return true;
}

bool Store(const char* path, const nightcoop_mac::save_package::SaveFile& save)
{
    const auto bytes = nightcoop_mac::save_package::Write(save);
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    std::printf("savepkg: wrote %s (%zu bytes)\n", path, bytes.size());
    return true;
}
}

int main(int argc, char** argv)
{
    using namespace nightcoop_mac::save_package;
    if (argc < 3)
    {
        std::printf("usage: nightcoop_savepkg info SAV | roundtrip IN OUT | treetrip SAV | splice HOST GUEST OUT [nodes...] | inventory HOST GUEST OUT\n");
        return 2;
    }
    const std::string command = argv[1];
    SaveFile save;
    if (!Load(argv[2], save))
    {
        return 1;
    }
    if (command == "info")
    {
        std::printf("savepkg: header %zu bytes, stream starts at %u, %zu unpacked bytes, %zu nodes\n", save.Header.size(), save.StreamStart,
                    save.Stream.size(), save.Nodes.size());
        for (int32_t index = 0; index >= 0 && index < static_cast<int32_t>(save.Nodes.size()); index = save.Nodes[index].Next)
        {
            std::printf("  %5d %-48s %10u bytes\n", index, save.Nodes[index].Name.c_str(), save.Nodes[index].Size);
        }
        return 0;
    }
    if (command == "roundtrip" && argc >= 4)
    {
        Store(argv[3], save);
        SaveFile back;
        if (!Load(argv[3], back))
        {
            return 1;
        }
        bool same = back.Stream == save.Stream && back.Header == save.Header && back.Nodes.size() == save.Nodes.size();
        for (size_t index = 0; same && index < save.Nodes.size(); ++index)
        {
            same = back.Nodes[index].Name == save.Nodes[index].Name && back.Nodes[index].Offset == save.Nodes[index].Offset &&
                   back.Nodes[index].Size == save.Nodes[index].Size && back.Nodes[index].Next == save.Nodes[index].Next &&
                   back.Nodes[index].Child == save.Nodes[index].Child;
        }
        std::printf("savepkg: round trip %s\n", same ? "reads back the same" : "DIFFERS");
        return same ? 0 : 1;
    }
    if (command == "treetrip")
    {
        std::vector<TreeNode> roots;
        std::string error;
        if (!ToTrees(save, roots, error))
        {
            std::printf("savepkg: trees: %s\n", error.c_str());
            return 1;
        }
        SaveFile rebuilt = save;
        FromTrees(rebuilt, roots);
        bool same = rebuilt.Stream == save.Stream && rebuilt.Nodes.size() == save.Nodes.size();
        for (size_t index = 0; same && index < save.Nodes.size(); ++index)
        {
            same = rebuilt.Nodes[index].Offset == save.Nodes[index].Offset && rebuilt.Nodes[index].Size == save.Nodes[index].Size &&
                   rebuilt.Nodes[index].Next == save.Nodes[index].Next && rebuilt.Nodes[index].Child == save.Nodes[index].Child;
        }
        std::printf("savepkg: %zu trees, rebuilt %s\n", roots.size(), same ? "the same" : "DIFFERENT");
        return same ? 0 : 1;
    }
    if (command == "inventory" && argc >= 5)
    {
        SaveFile guest;
        if (!Load(argv[3], guest))
        {
            return 1;
        }
        std::string error;
        if (!SpliceInventories(save, guest, {kPlayerInventory, kStashInventory}, error))
        {
            std::printf("savepkg: inventories: %s\n", error.c_str());
            return 1;
        }
        std::printf("savepkg: player and stash inventories came from the guest\n");
        Store(argv[4], save);
        SaveFile back;
        return Load(argv[4], back) ? 0 : 1;
    }
    if (command == "splice" && argc >= 5)
    {
        SaveFile guest;
        if (!Load(argv[3], guest))
        {
            return 1;
        }
        std::vector<std::string> names;
        for (int index = 5; index < argc; ++index)
        {
            names.push_back(argv[index]);
        }
        if (names.empty())
        {
            names = CharacterNodeNames();
        }
        int swapped = 0;
        for (const auto& name : names)
        {
            std::string error;
            if (SwapSubtree(save, guest, name, error))
            {
                ++swapped;
            }
            else
            {
                std::printf("savepkg: %s stays the hosts: %s\n", name.c_str(), error.c_str());
            }
        }
        std::printf("savepkg: %d of %zu node(s) came from the guest\n", swapped, names.size());
        Store(argv[4], save);
        SaveFile back;
        return Load(argv[4], back) ? 0 : 1;
    }
    std::printf("savepkg: unknown command %s\n", command.c_str());
    return 2;
}
