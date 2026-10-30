/*
 partner body builder, the mac take on nightcoops BodyBuilder
 nightcoops default partner body is a second PlayerPuppet built from the players own template. its build job
 (EntityBuilder_ExtractComponentsJob, address hash 489494088, verified in the mac 2.5.0 db) gets hooked and right before
 the job runs the freshly extracted objects of the template get edited, for partner bodies only:
   the 25 components a remote PlayerPuppet must not have (fpp camera, vehicle cameras, sound listener, hud and menu widgets,
   the player state machine, command consumer, garage, save, tier, mappin, breach finder, targeting / vision / scanning
   activators, trigger activator, interactions, inspection, disarm, debug visualizer, audio emitter, sensor object, weapon
   position, vehicle visual customization) get swapped for an empty turned off entSlotComponent named NightCoopRemoved_<name>
   with the same id, like nightcoop does, so the indices and ids in the package stay where they were
   then the body parts controller stops hiding parts in first person, attachment slot options stop moving items to the
   first person weapon plane and the animators render plane params stop following the graph (nightcoops three per component fixes)
 evry partner body (the V npc ones too) also gets nightcoops partner pass: its impostors are told the copy is done
 (gameImpostorComponent +0xB1, the same native byte as on windows, the rtti neighbours isCharacterReplica @0xB2 and the size
 0xF0 match) and its trigger activators are turned off. a body with customization parts but no body controller gets nightcoops
 NightCoopBodyController (a gameuiCharacterCustomizationBodyController, id = entity id ^ "NCBODYCT") added to its objects the
 way codeware adds components. the 25 component swap only happens on a PlayerPuppet root
 the job runs on worker threads and our own player comes from the same template, so a partner body is picked by the entity
 id codeware gave its spawn (ExpectBody, main thread, right after CreateEntity, long before the population system builds it).
 classes and properties get looked up on the main thread in PrepareForBodies, the job only reads them.
 wat the job did goes straight to the plugin log from the worker thread (the RED4ext logger is thread safe), so its there even
 when the game dies right after the build
 not ported (yet): copying our live players anim setup onto the body,
 nightcoops extra npc sense / hit / body controller components and its root swap for templates that are no PlayerPuppet
 offsets: job params {builder, weak builder}, builder +0x10 extractor, +0x90 template, +0xA0 request, request +0x20 entity id,
 extractor +0xA8 results, template +0x1D8 compiledDataHeader.rootIndex, component +0x8B isEnabled. same as windows and codeware
 for mac, and the mac rtti agrees where it can see them (template includeInstanceBuffer @0x1B0 and compiledData @0x248 sit right
 around the 0x70 byte header, component name @0x40, id @0x60, isReplicable @0x8C)
*/
#pragma once

#include <RED4ext/RED4ext.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace nightcoop_mac::partner_body_builder
{
using LogFunction = std::function<void(const std::string&)>;

bool Install(const RED4ext::v1::Sdk* sdk, RED4ext::v1::PluginHandle pluginHandle, LogFunction log);
bool PrepareForBodies();
void ExpectBody(uint64_t entityHash);
void ForgetBody(uint64_t entityHash);
void ForgetAllBodies();
std::vector<std::string> TakeLogLines();
}
