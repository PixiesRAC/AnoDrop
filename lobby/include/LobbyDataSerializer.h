#pragma once

#include <nlohmann/json.hpp>
#include "LobbyData.h"

void to_json(nlohmann::json &j, const FakeTimezone &tz);
void from_json(const nlohmann::json &j, FakeTimezone &tz);

void to_json(nlohmann::json &j, const ChatMessage &m);
void from_json(const nlohmann::json &j, ChatMessage &m);

void to_json(nlohmann::json &j, const LobbyHistoricalData &h);
void from_json(const nlohmann::json &j, LobbyHistoricalData &h);

void to_json(nlohmann::json &j, const AckState &a);
void from_json(const nlohmann::json &j, AckState &a);

void to_json(nlohmann::json &j, const LobbyData &l);
void from_json(const nlohmann::json &j, LobbyData &l);

void to_json(nlohmann::json &j, const PeerInfo &p);
void from_json(const nlohmann::json &j, PeerInfo &p);
