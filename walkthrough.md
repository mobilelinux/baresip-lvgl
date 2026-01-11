# Notifications and UI Updates (Final)

## Fixing "Phantom Calls" (Incoming Call Logic)
- **Problem**: Incoming calls were not matching the "User Agent" (UA), resulting in `UA=(nil)` logs. This caused the UI to be unanswerable and unstable (flashing).
- **Solution**: Implemented **Dynamic Catch-All Enforcement**.
  - Modified `baresip_manager.c` to listen for `BEVENT_REGISTERING`.
  - When an account registers, we explicitly call `account_set_catchall(acc, true)`.
  - This ensures the UA accepts calls even if the `Request-URI` or `To` header doesn't perfectly match the local configuration (common in NAT/QEMU scenarios).

## Message Badges
- **Fix**: Corrected badge logic to match applet name "Messages".
- **Functionality**: Red badge appears on "Messages" tile when new messages arrive.
- **Persistence**: Badge persists across restarts until messages are read.

## Missed Call Badges
- **Feature**: Red badge on "Call" tile for missed calls.
- **Phantom Call Handling**: With the "Catch-All" fix, phantom calls should be eliminated. However, fallback logging handles any remaining edge cases.
- **Clearance**: Opening the Dialer calls `db_call_mark_all_viewed()`, clearing the badge.

## UI Polish
- **Call Log**: Removed extraneous scrollbars from individual call log items and headers for a cleaner list view.
- **Message List**: Removed extraneous scrollbars from individual message thread items.

## Verification Checklist
1.  [x] **Build**: `make clean && make` passed.
2.  [ ] **Run**: Verify log `BaresipManager: Dynamic Catch-All ENABLED for ...`
3.  [ ] **Incoming Call**: UI appears stable, "Answer" button works.
4.  [ ] **Badges**: Receive message/call -> Verify Red Badges on Home Screen.
