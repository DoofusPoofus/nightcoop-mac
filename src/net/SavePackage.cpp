#include "net/SavePackage.hpp"

#include <algorithm>
#include <cstring>

namespace nightcoop_mac::save_package
{
namespace
{
constexpr uint32_t ChunkSlots = 512;
constexpr uint32_t ChunkBytes = 0x40000;
constexpr uint32_t MostUnpackedChunk = 0x4000000;
constexpr size_t HashBits = 16;

uint32_t ReadU32(const std::vector<uint8_t>& bytes, size_t offset)
{
    uint32_t value = 0;
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}

void PutU32(std::vector<uint8_t>& bytes, uint32_t value)
{
    const auto* raw = reinterpret_cast<const uint8_t*>(&value);
    bytes.insert(bytes.end(), raw, raw + sizeof(value));
}

void PutU32At(std::vector<uint8_t>& bytes, size_t offset, uint32_t value)
{
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

bool ReadPackedInt(const std::vector<uint8_t>& bytes, size_t& position, int64_t& value)
{
    if (position >= bytes.size())
    {
        return false;
    }
    uint8_t current = bytes[position++];
    const bool negative = (current & 0x80) != 0;
    uint64_t magnitude = current & 0x3F;
    bool more = (current & 0x40) != 0;
    int shift = 6;
    while (more)
    {
        if (position >= bytes.size() || shift > 56)
        {
            return false;
        }
        current = bytes[position++];
        magnitude |= static_cast<uint64_t>(current & 0x7F) << shift;
        shift += 7;
        more = (current & 0x80) != 0;
    }
    value = negative ? -static_cast<int64_t>(magnitude) : static_cast<int64_t>(magnitude);
    return true;
}

void PutPackedInt(std::vector<uint8_t>& bytes, int64_t value)
{
    const bool negative = value < 0;
    uint64_t magnitude = negative ? static_cast<uint64_t>(-value) : static_cast<uint64_t>(value);
    uint8_t first = static_cast<uint8_t>(magnitude & 0x3F) | (negative ? 0x80 : 0);
    magnitude >>= 6;
    if (magnitude)
    {
        first |= 0x40;
    }
    bytes.push_back(first);
    while (magnitude)
    {
        uint8_t next = static_cast<uint8_t>(magnitude & 0x7F);
        magnitude >>= 7;
        if (magnitude)
        {
            next |= 0x80;
        }
        bytes.push_back(next);
    }
}

void PutLength(std::vector<uint8_t>& output, size_t length)
{
    while (length >= 255)
    {
        output.push_back(255);
        length -= 255;
    }
    output.push_back(static_cast<uint8_t>(length));
}

void PutSequence(std::vector<uint8_t>& output, const uint8_t* literals, size_t literalCount, uint32_t matchOffset, size_t matchLength)
{
    const size_t matchCode = matchLength ? matchLength - 4 : 0;
    const uint8_t token = static_cast<uint8_t>((std::min<size_t>(literalCount, 15) << 4) | (matchLength ? std::min<size_t>(matchCode, 15) : 0));
    output.push_back(token);
    if (literalCount >= 15)
    {
        PutLength(output, literalCount - 15);
    }
    output.insert(output.end(), literals, literals + literalCount);
    if (!matchLength)
    {
        return;
    }
    output.push_back(static_cast<uint8_t>(matchOffset & 0xFF));
    output.push_back(static_cast<uint8_t>(matchOffset >> 8));
    if (matchCode >= 15)
    {
        PutLength(output, matchCode - 15);
    }
}

std::vector<int32_t> Subtree(const SaveFile& save, int32_t root)
{
    std::vector<int32_t> order {root};
    for (int32_t child = save.Nodes[root].Child; child >= 0 && child < static_cast<int32_t>(save.Nodes.size()); child = save.Nodes[child].Next)
    {
        const auto below = Subtree(save, child);
        order.insert(order.end(), below.begin(), below.end());
    }
    return order;
}
}

std::vector<uint8_t> PackLz4Block(const uint8_t* source, size_t size)
{
    std::vector<uint8_t> output;
    output.reserve(size / 2 + 16);
    std::vector<int64_t> lastSeen(size_t {1} << HashBits, -1);
    size_t anchor = 0;
    size_t position = 0;
    const size_t matchLimit = size > 12 ? size - 12 : 0;
    while (position < matchLimit)
    {
        uint32_t sequence = 0;
        std::memcpy(&sequence, source + position, sizeof(sequence));
        const size_t hash = (sequence * 2654435761u) >> (32 - HashBits);
        const int64_t candidate = lastSeen[hash];
        lastSeen[hash] = static_cast<int64_t>(position);
        uint32_t candidateSequence = 0;
        if (candidate >= 0 && position - static_cast<size_t>(candidate) <= 65535)
        {
            std::memcpy(&candidateSequence, source + candidate, sizeof(candidateSequence));
        }
        if (candidate < 0 || position - static_cast<size_t>(candidate) > 65535 || candidateSequence != sequence)
        {
            ++position;
            continue;
        }
        size_t length = 4;
        while (position + length < size - 5 && source[candidate + length] == source[position + length])
        {
            ++length;
        }
        PutSequence(output, source + anchor, position - anchor, static_cast<uint32_t>(position - candidate), length);
        position += length;
        anchor = position;
    }
    PutSequence(output, source + anchor, size - anchor, 0, 0);
    return output;
}

bool UnpackLz4Block(const uint8_t* source, size_t size, std::vector<uint8_t>& output, size_t expectedSize)
{
    output.clear();
    output.reserve(expectedSize);
    size_t position = 0;
    while (position < size)
    {
        const uint8_t token = source[position++];
        size_t literalCount = token >> 4;
        if (literalCount == 15)
        {
            uint8_t extra = 255;
            while (extra == 255 && position < size)
            {
                extra = source[position++];
                literalCount += extra;
            }
        }
        if (position + literalCount > size || output.size() + literalCount > expectedSize)
        {
            return false;
        }
        output.insert(output.end(), source + position, source + position + literalCount);
        position += literalCount;
        if (position >= size)
        {
            break;
        }
        if (position + 2 > size)
        {
            return false;
        }
        const size_t matchOffset = source[position] | (source[position + 1] << 8);
        position += 2;
        size_t matchLength = token & 15;
        if (matchLength == 15)
        {
            uint8_t extra = 255;
            while (extra == 255 && position < size)
            {
                extra = source[position++];
                matchLength += extra;
            }
        }
        matchLength += 4;
        if (matchOffset == 0 || matchOffset > output.size() || output.size() + matchLength > expectedSize)
        {
            return false;
        }
        const size_t start = output.size() - matchOffset;
        for (size_t index = 0; index < matchLength; ++index)
        {
            output.push_back(output[start + index]);
        }
    }
    return output.size() == expectedSize;
}

bool Read(const std::vector<uint8_t>& bytes, SaveFile& save, std::string& error)
{
    save = SaveFile {};
    if (bytes.size() < 64 || std::memcmp(bytes.data(), "VASC", 4) != 0)
    {
        error = "no VASC header";
        return false;
    }
    size_t tableStart = 0;
    for (size_t offset = 4; offset + 4 <= 64; ++offset)
    {
        if (std::memcmp(bytes.data() + offset, "FZLC", 4) == 0)
        {
            tableStart = offset;
            break;
        }
    }
    if (!tableStart || tableStart + 8 + ChunkSlots * 12 > bytes.size())
    {
        error = "no FZLC chunk table";
        return false;
    }
    save.Header.assign(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(tableStart));
    save.StreamStart = static_cast<uint32_t>(tableStart + 8 + ChunkSlots * 12);
    const uint32_t chunkCount = ReadU32(bytes, tableStart + 4);
    if (chunkCount == 0 || chunkCount > ChunkSlots)
    {
        error = "chunk count " + std::to_string(chunkCount);
        return false;
    }
    std::vector<uint8_t> unpacked;
    for (uint32_t chunk = 0; chunk < chunkCount; ++chunk)
    {
        const size_t slot = tableStart + 8 + chunk * 12;
        const uint32_t offset = ReadU32(bytes, slot);
        const uint32_t packedSize = ReadU32(bytes, slot + 4);
        const uint32_t unpackedSize = ReadU32(bytes, slot + 8);
        if (packedSize < 8 || static_cast<size_t>(offset) + packedSize > bytes.size() || std::memcmp(bytes.data() + offset, "4ZLX", 4) != 0 ||
            unpackedSize > MostUnpackedChunk || ReadU32(bytes, offset + 4) != unpackedSize)
        {
            error = "chunk " + std::to_string(chunk) + " looks broken";
            return false;
        }
        if (!UnpackLz4Block(bytes.data() + offset + 8, packedSize - 8, unpacked, unpackedSize))
        {
            error = "chunk " + std::to_string(chunk) + " does not unpack";
            return false;
        }
        save.Stream.insert(save.Stream.end(), unpacked.begin(), unpacked.end());
    }
    if (std::memcmp(bytes.data() + bytes.size() - 4, "ENOD", 4) != 0)
    {
        error = "no ENOD footer";
        return false;
    }
    size_t position = ReadU32(bytes, bytes.size() - 8);
    if (position + 4 > bytes.size() || std::memcmp(bytes.data() + position, "EDON", 4) != 0)
    {
        error = "no EDON node table";
        return false;
    }
    position += 4;
    int64_t nodeCount = 0;
    if (!ReadPackedInt(bytes, position, nodeCount) || nodeCount <= 0 || nodeCount > 1000000)
    {
        error = "bad node count";
        return false;
    }
    for (int64_t index = 0; index < nodeCount; ++index)
    {
        int64_t length = 0;
        if (!ReadPackedInt(bytes, position, length))
        {
            error = "bad node name";
            return false;
        }
        const size_t nameLength = static_cast<size_t>(length < 0 ? -length : length);
        if (position + nameLength + 16 > bytes.size())
        {
            error = "node table cut short";
            return false;
        }
        SaveNode node;
        node.Name.assign(reinterpret_cast<const char*>(bytes.data() + position), nameLength);
        position += nameLength;
        node.Next = static_cast<int32_t>(ReadU32(bytes, position));
        node.Child = static_cast<int32_t>(ReadU32(bytes, position + 4));
        node.Offset = ReadU32(bytes, position + 8);
        node.Size = ReadU32(bytes, position + 12);
        position += 16;
        save.Nodes.push_back(std::move(node));
    }
    return CheckTree(save, error);
}

std::vector<uint8_t> Write(const SaveFile& save)
{
    std::vector<uint8_t> output(save.Header);
    output.insert(output.end(), {'F', 'Z', 'L', 'C'});
    const uint32_t chunkCount = static_cast<uint32_t>((save.Stream.size() + ChunkBytes - 1) / ChunkBytes);
    PutU32(output, chunkCount);
    const size_t slotsStart = output.size();
    output.resize(slotsStart + ChunkSlots * 12, 0);
    for (uint32_t chunk = 0; chunk < chunkCount; ++chunk)
    {
        const size_t start = static_cast<size_t>(chunk) * ChunkBytes;
        const size_t length = std::min<size_t>(ChunkBytes, save.Stream.size() - start);
        const auto packed = PackLz4Block(save.Stream.data() + start, length);
        const uint32_t offset = static_cast<uint32_t>(output.size());
        output.insert(output.end(), {'4', 'Z', 'L', 'X'});
        PutU32(output, static_cast<uint32_t>(length));
        output.insert(output.end(), packed.begin(), packed.end());
        PutU32At(output, slotsStart + chunk * 12, offset);
        PutU32At(output, slotsStart + chunk * 12 + 4, static_cast<uint32_t>(packed.size() + 8));
        PutU32At(output, slotsStart + chunk * 12 + 8, static_cast<uint32_t>(length));
    }
    const uint32_t tableOffset = static_cast<uint32_t>(output.size());
    output.insert(output.end(), {'E', 'D', 'O', 'N'});
    PutPackedInt(output, static_cast<int64_t>(save.Nodes.size()));
    for (const auto& node : save.Nodes)
    {
        PutPackedInt(output, -static_cast<int64_t>(node.Name.size()));
        output.insert(output.end(), node.Name.begin(), node.Name.end());
        PutU32(output, static_cast<uint32_t>(node.Next));
        PutU32(output, static_cast<uint32_t>(node.Child));
        PutU32(output, node.Offset);
        PutU32(output, node.Size);
    }
    PutU32(output, tableOffset);
    output.insert(output.end(), {'E', 'N', 'O', 'D'});
    return output;
}

int32_t FindTopLevel(const SaveFile& save, const std::string& name)
{
    for (int32_t index = 0; index >= 0 && index < static_cast<int32_t>(save.Nodes.size()); index = save.Nodes[index].Next)
    {
        if (save.Nodes[index].Name == name)
        {
            return index;
        }
    }
    return -1;
}

bool SwapSubtree(SaveFile& host, const SaveFile& guest, const std::string& name, std::string& error)
{
    const int32_t hostRoot = FindTopLevel(host, name);
    const int32_t guestRoot = FindTopLevel(guest, name);
    if (hostRoot < 0 || guestRoot < 0)
    {
        error = name + " is missing in " + (hostRoot < 0 ? "the host save" : "our save");
        return false;
    }
    const auto hostOrder = Subtree(host, hostRoot);
    const auto guestOrder = Subtree(guest, guestRoot);
    if (hostOrder.size() != guestOrder.size())
    {
        error = name + " has " + std::to_string(guestOrder.size()) + " node(s) in our save and " + std::to_string(hostOrder.size()) + " in the hosts";
        return false;
    }
    for (size_t index = 0; index < hostOrder.size(); ++index)
    {
        if (host.Nodes[hostOrder[index]].Name != guest.Nodes[guestOrder[index]].Name)
        {
            error = name + " has other child nodes in our save than in the hosts";
            return false;
        }
    }
    const SaveNode hostNode = host.Nodes[hostRoot];
    const SaveNode& guestNode = guest.Nodes[guestRoot];
    const size_t guestStart = guestNode.Offset - guest.StreamStart;
    std::vector<uint8_t> replacement(guest.Stream.begin() + static_cast<std::ptrdiff_t>(guestStart),
                                     guest.Stream.begin() + static_cast<std::ptrdiff_t>(guestStart + guestNode.Size));
    for (size_t index = 0; index < guestOrder.size(); ++index)
    {
        const size_t relative = guest.Nodes[guestOrder[index]].Offset - guestNode.Offset;
        if (relative + 4 > replacement.size() || ReadU32(replacement, relative) != static_cast<uint32_t>(guestOrder[index]))
        {
            error = name + " in our save does not start its nodes with their index";
            return false;
        }
        PutU32At(replacement, relative, static_cast<uint32_t>(hostOrder[index]));
    }
    const int64_t delta = static_cast<int64_t>(guestNode.Size) - static_cast<int64_t>(hostNode.Size);
    const size_t hostStart = hostNode.Offset - host.StreamStart;
    host.Stream.erase(host.Stream.begin() + static_cast<std::ptrdiff_t>(hostStart),
                      host.Stream.begin() + static_cast<std::ptrdiff_t>(hostStart + hostNode.Size));
    host.Stream.insert(host.Stream.begin() + static_cast<std::ptrdiff_t>(hostStart), replacement.begin(), replacement.end());
    std::vector<bool> inSubtree(host.Nodes.size(), false);
    for (int32_t index : hostOrder)
    {
        inSubtree[index] = true;
    }
    const uint64_t hostEnd = static_cast<uint64_t>(hostNode.Offset) + hostNode.Size;
    for (size_t index = 0; index < host.Nodes.size(); ++index)
    {
        if (inSubtree[index])
        {
            continue;
        }
        auto& node = host.Nodes[index];
        if (node.Offset >= hostEnd)
        {
            node.Offset = static_cast<uint32_t>(node.Offset + delta);
        }
        else if (node.Offset <= hostNode.Offset && static_cast<uint64_t>(node.Offset) + node.Size >= hostEnd)
        {
            node.Size = static_cast<uint32_t>(node.Size + delta);
        }
    }
    for (size_t index = 0; index < hostOrder.size(); ++index)
    {
        const auto& from = guest.Nodes[guestOrder[index]];
        auto& to = host.Nodes[hostOrder[index]];
        to.Offset = hostNode.Offset + (from.Offset - guestNode.Offset);
        to.Size = from.Size;
    }
    return CheckTree(host, error);
}

bool CheckTree(const SaveFile& save, std::string& error)
{
    const uint64_t streamEnd = static_cast<uint64_t>(save.StreamStart) + save.Stream.size();
    for (size_t index = 0; index < save.Nodes.size(); ++index)
    {
        const auto& node = save.Nodes[index];
        if (node.Offset < save.StreamStart || static_cast<uint64_t>(node.Offset) + node.Size > streamEnd || node.Size < 4)
        {
            error = "node " + node.Name + " lies outside the data";
            return false;
        }
        if (ReadU32(save.Stream, node.Offset - save.StreamStart) != index)
        {
            error = "node " + node.Name + " does not start with its index";
            return false;
        }
        for (int32_t child = node.Child; child >= 0; child = save.Nodes[child].Next)
        {
            if (child >= static_cast<int32_t>(save.Nodes.size()))
            {
                error = "node " + node.Name + " has a child past the table";
                return false;
            }
            const auto& inner = save.Nodes[child];
            if (inner.Offset < node.Offset || static_cast<uint64_t>(inner.Offset) + inner.Size > static_cast<uint64_t>(node.Offset) + node.Size)
            {
                error = "child " + inner.Name + " lies outside " + node.Name;
                return false;
            }
        }
    }
    uint64_t lastEnd = 0;
    for (int32_t index = 0; index >= 0 && index < static_cast<int32_t>(save.Nodes.size()); index = save.Nodes[index].Next)
    {
        const auto& node = save.Nodes[index];
        if (node.Offset < lastEnd)
        {
            error = "top level node " + node.Name + " overlaps the one before";
            return false;
        }
        lastEnd = static_cast<uint64_t>(node.Offset) + node.Size;
    }
    return true;
}
}

namespace nightcoop_mac::save_package
{
const std::vector<std::string>& CharacterNodeNames()
{
    static const std::vector<std::string> names {"CharacetrCustomization_Appearances", "WardrobeSystem", "WardrobeSystem_ClothingSets", "StatPoolsSystem",
                                                 "StatsDataSystem", "photoModeSystem", "PhotoMode_Settings", "PhotoMode_NewStickersEditor",
                                                 "PhotoMode_LightSettings", "PhotoMode_OutfitWeather", "TutorialManager", "AchievementSystem",
                                                 "TelemetrySystem", "ArcadeSystem", "ControlledLootIntroduction", "HandicapAmmoInContainers",
                                                 "LootManager_PlaythroughSeed"};
    return names;
}

int SpliceCharacter(SaveFile& host, const SaveFile& guest, std::vector<std::string>& problems)
{
    int swapped = 0;
    for (const auto& name : CharacterNodeNames())
    {
        std::string error;
        if (SwapSubtree(host, guest, name, error))
        {
            ++swapped;
        }
        else
        {
            problems.push_back(error);
        }
    }
    return swapped;
}
}
