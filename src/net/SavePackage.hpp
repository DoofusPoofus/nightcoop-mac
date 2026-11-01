/*
 save package reads and writes cyberpunk sav.dat files like nightcoops savepkg so a guest can play its own V in the hosts world
 sav.dat: 25 byte header (VASC save version game version ...) | FZLC u32 chunk count then 512 slots of (u32 file offset |
 u32 packed size | u32 unpacked size) | the chunks (4ZLX u32 unpacked size then an LZ4 block 256 KiB unpacked each) |
 EDON packed count then per node (packed length string name | i32 next sibling | i32 first child | u32 offset | u32 size) |
 u32 file offset of the node table | ENOD. node offsets point into the unpacked stream which starts right after the slot
 table (6177). evry node payload starts with its own index as u32 and children sit inside their parents range
 SwapSubtree puts a node (and its children) of another save in place of the same named node of this one when both subtrees
 have the same names in the same order: the bytes get replaced the indices at each node start get rewritten offsets and sizes
 of evry node after or around it get moved. Write packs the stream again in 256 KiB chunks with a greedy LZ4 compressor
 SpliceCharacter swaps the character nodes nightcoop takes from the guest that can go whole (look wardrobe stat pools stats
 data photo mode tutorial achievements telemetry arcade loot intro handicap ammo playthrough loot seed)
 ToTrees / FromTrees turn the node table into trees of byte pieces around the children and back (renumbering evry node so
 subtrees can come and go. loose bytes between top level nodes ride along as their leading bytes). SpliceInventories uses them on the inventory node (u32 index | u32 count | per sub inventory u64
 id | u32 items | per item 16 byte item id + its itemData node) to take the player (0x1) and stash (0xF4240) sub inventories
 from the guest
*/
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nightcoop_mac::save_package
{
struct SaveNode
{
    std::string Name;
    int32_t Next = -1;
    int32_t Child = -1;
    uint32_t Offset = 0;
    uint32_t Size = 0;
};

struct SaveFile
{
    std::vector<uint8_t> Header;
    uint32_t StreamStart = 0;
    std::vector<uint8_t> Stream;
    std::vector<SaveNode> Nodes;
};

struct TreeNode
{
    std::vector<uint8_t> Leading;
    std::string Name;
    std::vector<std::vector<uint8_t>> Pieces;
    std::vector<TreeNode> Children;
};

inline constexpr uint64_t kPlayerInventory = 0x1;
inline constexpr uint64_t kStashInventory = 0xF4240;

bool Read(const std::vector<uint8_t>& bytes, SaveFile& save, std::string& error);
bool ToTrees(const SaveFile& save, std::vector<TreeNode>& roots, std::string& error);
void FromTrees(SaveFile& save, const std::vector<TreeNode>& roots);
bool SpliceInventories(SaveFile& host, const SaveFile& guest, const std::vector<uint64_t>& inventoryIds, std::string& error);
std::vector<uint8_t> Write(const SaveFile& save);
int32_t FindTopLevel(const SaveFile& save, const std::string& name);
bool SwapSubtree(SaveFile& host, const SaveFile& guest, const std::string& name, std::string& error);
bool CheckTree(const SaveFile& save, std::string& error);
const std::vector<std::string>& CharacterNodeNames();
int SpliceCharacter(SaveFile& host, const SaveFile& guest, std::vector<std::string>& problems);
std::vector<uint8_t> PackLz4Block(const uint8_t* source, size_t size);
bool UnpackLz4Block(const uint8_t* source, size_t size, std::vector<uint8_t>& output, size_t expectedSize);
}
