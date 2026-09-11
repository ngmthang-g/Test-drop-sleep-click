# TEST TRADE STATE V4.2 — OnReceivePacket(System.Byte[])

V4.2 is based on the live-client signature proven by V4.1:

`LuaSystemManager.OnReceivePacket(System.Byte[]) -> System.Void`

## What V4.2 does

- Hooks only the confirmed `OnReceivePacket(System.Byte[])` entry.
- Does not invoke Lua, UI, `GetScript`, OCR, bag scan, or per-item tracking.
- Reads the incoming managed byte array passively and returns immediately to the original method.
- Searches the bounded raw frame for exact representations of `CMD_TRADE_DATA = 200053`:
  - LE32: `75 0D 03 00`
  - BE32: `00 03 0D 75`
  - ASCII: `200053`
  - UTF-16LE: `200053`
  - unsigned varint: `F5 9A 0C`
- If an exact 200053 candidate is present, it also searches after it for `-1` (ASCII/UTF-16LE), because DATA 222 verifies `Trade:LoadData("-1")` as the close path.

## Important evidence rule

V4.2 labels raw matches as **candidate** until live tests prove the framing. It does not pretend that the raw byte layout is already documented in DATA 222.

The first raw packet received changes the log to `V4.2 LIVE`. The status field continuously shows a bounded HEX sample in `last=...`, even if 200053 is not found. This proves whether the byte[] hook itself is receiving traffic.

## Test

1. Restart all game clients before changing bridge versions.
2. Keep `ThanLongTradeStateTest.exe` and `ThanLongTradePacketBridge.dll` in the same folder.
3. Select one PID and start monitoring.
4. Confirm the log shows the exact signature `OnReceivePacket(System.Byte[])` and then `V4.2 LIVE` when network packets arrive.
5. Perform one successful Trade and one cancelled Trade.
6. Save/screenshot the whole log and the status line.

If V4.2 sees 200053, the log reports encoding, byte offset and a HEX window. If it does not see 200053 in raw bytes, the HEX sample still gives runtime evidence for the next parser step without calling any Lua/runtime function.

Build marker: V4.2 workflow enabled.