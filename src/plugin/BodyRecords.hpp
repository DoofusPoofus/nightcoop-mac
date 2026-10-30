/*
 body records: character records for PlayerPuppet partner bodies, the mac take on nightcoops record making (its "_g" records)
 codeware can only spawn from a record or a template, and a template alone gets an invented SpawnableObject record that the
 bodys look parts crash on. Character.Player_Puppet_Base only brings the base female template. so like nightcoop this clones
 that record per template: Character.Player_Puppet_Base_g<template hash> gets a copy of evry flat of the base record (found by
 the record classes getter names, each copied flat points at the same value), its own entityTemplatePath and the genders of
 the cutscene V record of that gender, then TweakDB makes the record from those flats (TweakDB_Get and TweakDB_CreateRecord
 are verified in the mac 2.5.0 db, the rest is the sdks flat bookkeeping). main thread only
*/
#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace nightcoop_mac::body_records
{
using LogFunction = std::function<void(const std::string&)>;

uint64_t PlayerBodyRecord(const std::string& templatePath, bool male, const LogFunction& log);
}
