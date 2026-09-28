-- STR Plugin Messaging chat relay for unmodified Skyrim Together servers.
--
-- This resource only handles server-side routing. The client bridge injects
-- STRPM envelopes into STR chat and consumes the relayed envelopes.

local PREFIX = "STRPM|v2|"
local IDENTITY_CHANNEL = "strpm.identity.v1"
local MAX_ENVELOPE_LENGTH = 4096
local MAX_CHANNEL_LENGTH = 96
local MAX_PARTS = 64
local FLAG_ALLOW_LOOPBACK = 4

local gameServer = GameServer:get()
local playerManager = PlayerManager:get()

local function startsWith(value, prefix)
  return string.sub(value, 1, string.len(prefix)) == prefix
end

local function getPlayerByCharacter(entityId)
  local allPlayers = playerManager:GetAllPlayers()
  for _, player in ipairs(allPlayers) do
    if player:GetCharacter() == entityId then
      return player
    end
  end
  return nil
end

local function splitFields(envelope)
  local fields = {}
  for token in string.gmatch(envelope, "([^|]+)") do
    local separator = string.find(token, "=", 1, true)
    if separator then
      local key = string.sub(token, 1, separator - 1)
      local value = string.sub(token, separator + 1)
      fields[key] = value
    end
  end
  return fields
end

local function validUnsigned(value)
  return value ~= nil and string.match(value, "^%d+$") ~= nil
end

local function validChannel(value)
  return value ~= nil
    and string.len(value) > 0
    and string.len(value) <= MAX_CHANNEL_LENGTH
    and string.match(value, "^[A-Za-z0-9._%-]+$") ~= nil
end

local function safeField(value)
  local result = tostring(value or "")
  result = string.gsub(result, "[|\r\n]", "_")
  return result
end

local function flagIsSet(flags, flag)
  return math.floor(flags / flag) % 2 == 1
end

local function hexEncodeAscii(value)
  return (string.gsub(value, ".", function(character)
    return string.format("%02X", string.byte(character))
  end))
end

local function replacePayload(envelope, payloadHex)
  local replaced, count = string.gsub(
    envelope,
    "(|payload=)[0-9A-Fa-f]*",
    "%1" .. payloadHex,
    1)
  if count == 1 then
    return replaced
  end
  return envelope
end

local function validateEnvelope(fields)
  if fields["msg"] == nil or not validUnsigned(fields["seq"]) then
    return false, "missing msg/seq"
  end
  if not validChannel(fields["channel"]) then
    return false, "invalid channel"
  end
  if fields["payload"] == nil or string.match(fields["payload"], "^[0-9A-Fa-f]*$") == nil then
    return false, "invalid payload"
  end
  if not validUnsigned(fields["flags"]) then
    return false, "invalid flags"
  end
  if not validUnsigned(fields["part"]) or not validUnsigned(fields["parts"]) then
    return false, "invalid fragment metadata"
  end

  local part = tonumber(fields["part"])
  local parts = tonumber(fields["parts"])
  if part == nil or parts == nil or parts < 1 or parts > MAX_PARTS or part < 1 or part > parts then
    return false, "fragment out of range"
  end

  local target = fields["target"] or "all"
  if target ~= "all" and target ~= "server" and string.match(target, "^id:%d+$") == nil then
    return false, "invalid target"
  end

  return true, nil
end

local function relayToTarget(target, envelope, senderConnectionId, flags)
  local allowLoopback = flagIsSet(flags, FLAG_ALLOW_LOOPBACK)

  if target == nil or target == "" or target == "all" then
    local allPlayers = playerManager:GetAllPlayers()
    for _, player in ipairs(allPlayers) do
      local connectionId = player:GetConnectionId()
      if allowLoopback or connectionId ~= senderConnectionId then
        gameServer:SendChatMessage(connectionId, envelope)
      end
    end
    return true
  end

  local id = string.match(target, "^id:(%d+)$")
  if id ~= nil then
    local connectionId = tonumber(id)
    if allowLoopback or connectionId ~= senderConnectionId then
      gameServer:SendChatMessage(connectionId, envelope)
    end
    return true
  end

  if target == "server" then
    return true
  end

  return false
end

addEventHandler("onChatMessage", function(entityId, message)
  if type(message) ~= "string" or not startsWith(message, PREFIX) then
    return
  end

  cancelEvent("STRPM transport envelope")

  if string.len(message) > MAX_ENVELOPE_LENGTH then
    print("[STRPM] Dropped oversized envelope")
    return
  end

  local player = getPlayerByCharacter(entityId)
  if player == nil then
    print("[STRPM] Dropped envelope from unknown character " .. tostring(entityId))
    return
  end

  local fields = splitFields(message)
  local valid, reason = validateEnvelope(fields)
  if not valid then
    print("[STRPM] Dropped malformed envelope: " .. tostring(reason))
    return
  end

  local senderConnectionId = player:GetConnectionId()
  local senderPlayerId = player:GetId()
  local flags = tonumber(fields["flags"]) or 0

  -- Identity heartbeats originate with an empty payload. Replace that payload
  -- server-side with the authenticated STR PlayerId before relaying it. Peers
  -- can then consume ConnectionID + PlayerId through the normal deferred STRPM
  -- receive pipeline instead of depending on VEH observer ordering.
  if fields["channel"] == IDENTITY_CHANNEL then
    message = replacePayload(message, hexEncodeAscii(tostring(senderPlayerId)))
  end

  local relayEnvelope =
    message ..
    "|sender=" .. tostring(senderConnectionId) ..
    "|senderPlayerId=" .. tostring(senderPlayerId) ..
    "|senderName=" .. safeField(player:GetUsername()) ..
    "|serverTick=" .. tostring(gameServer:GetTick())

  if not relayToTarget(fields["target"], relayEnvelope, senderConnectionId, flags) then
    print("[STRPM] Dropped envelope with unsupported target")
  end
end)

print("[STRPM] Chat relay v4 loaded (authenticated identity payload enabled)")
