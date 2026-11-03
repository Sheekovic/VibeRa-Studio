# MIDI controller binding configuration ? development source

Discover vibera_controller on the connected executable. Production actions are
state and setBinding. Preset, whole-profile configure, learn, cancel and inject
remain owned --mcp-test diagnostics; no production MIDI injection, hardware-port
selection or caller-selected settings filepath is provided. Arbitrary native
plugin UI and physical MIDI operation are separate evidence.

Read {"action":"state"}. The previous whole-application state shape remains;
controllerProfile contains actual saved-format1 metadata and bindings. Additional
controllerProfileIdentity identifies current runtime profile AND available saved
file bytes. controllerProfileIdentityKnown=false means preserve mappings and
inspect controllerProfileIdentityError before attempting changes. A genuinely
missing preferences file has a valid fresh identity; it is not an error.

controllerBindingPersistence is persistentSettings for normal Settings storage,
or sessionOnly in an isolated nonpersistent Studio. Read-only state never opens
ports. controllerParameterActions and controllerBindingCommands derive supported
actions from existing canonical registries. Query vibera_commands for action
behavior; a mapping is not proof the target is currently available or acted on.

To edit, copy a complete existing binding or build one from the discovered binding
schema, then call setBinding with index, binding and exact expectedProfile from
state. Index is an existing row, or exactly the current bindings length to append;
no gaps and maximum512. Every binding field is required except sendTarget, which
is required ONLY for action=mixer.send. Unknown/wrong-typed fields refuse.

Binding fields:

- action/label: supported canonical command or macro ID; label maximum120.
- port:0 performance ingress or1 control ingress; metadata only, never device setup.
- channel:0 all or1..16; kind:0 CC,1 note,2 program,3 pitch bend,4 Start,5 Stop,6 Continue.
- number:0..127; layer:0..3; mode:0 press,1 absolute,2 relative twos-complement,
  3 relative offset,4 modifier hold. Kind/mode combinations use the same Settings validator.
- mixer:-1 selected,0 Master, positive fixed stable source ID; banked positives
  are1-based display positions excluding Master. Bank capacity follows current model.
- minimum/maximum: finite ordered range; mixer.send specifically0..2 linear gain.
- pickup/banked/enabled:boolean; nativeArea:0 normal,1 MK3 pots,2 MK3 faders.
  Existing exact Launchkey native port/channel/CC/mode validation remains.
- sendTarget: explicit stable destination,0 Master, ONLY for mixer.send. Send
  requires continuous absolute/relative mode and nativeArea0. Actual invocation
  validates both targets and the enabled route; missing IDs/routes remain stored
  and refuse rather than clamp or select a replacement.

Example full mapping (use an intentional existing row or the exact append index,
and replace the token with actual state):

```json
{"action":"setBinding","index":0,"expectedProfile":"<actual64hex>","binding":{"action":"mixer.send","label":"Send to Master","port":1,"channel":1,"kind":0,"number":23,"layer":0,"mode":1,"mixer":1,"sendTarget":0,"minimum":0,"maximum":2,"pickup":true,"banked":false,"enabled":true,"nativeArea":0}}
```

setBinding shares the Settings Apply service. Candidate validation/conflict checks
and durable atomic preferences replacement happen BEFORE mapping publication.
Save/refusal preserves previous bindings, mapping queues/generation, learning and
port handles; success uses existing publication reset policy (including clearing
old queued control events/pressed state and ending Learn), without port close/reopen.
Never treat failure as a request to delete settings or reconfigure hardware.

Success includes controllerBindingChange with configurationCommitted=true,
persisted=true only for the actual normal persistent save path, projectUndoable=false,
completionScope=controllerBindingConfiguration. Session-only tests truthfully
return persisted=false. This is synchronous configuration completion, not Project
Undo, MIDI delivery, DSP settling or heard audio. Re-read actual binding and fresh
token. Mapping changes do not dirty/advance musical Project history.

Errors include controller.invalidBinding, controller.bindingConflict,
controller.invalidIndex, controller.staleProfile, controller.profileUnreadable,
controller.profileTooLarge, controller.profileSaveFailed and
controller.preparationFailed. Stale refuses before publication; refresh state
and deliberately reconcile. The token protects this runtime/saved snapshot,
not a general multi-process settings merge or filesystem transaction. There is
no cross-process guarantee against an external writer racing the final atomic
replacement after the last check. Use optional current projectIdentity guard for
ID-sensitive project intent, and re-query current targets after project changes.

UI Add/Remove/preset/port/learn workflows and their inherited broader persistence
or semantic gaps remain separate; this increment closes the changed single-binding
Apply/send-target operation, not the full legacy controller/MCP campaign. Actual
physical hardware, pickup response and production-port lifecycle need separate
current qualification. Do not equate source/core metadata tests with those results.
