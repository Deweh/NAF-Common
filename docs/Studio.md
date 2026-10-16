# NAF Studio — Technical Design

NAF Studio is the in-game animation editor. It's made of three parts:

| Part | Files | Role |
|---|---|---|
| **ProjectEx** | `project/src/Animation/Studio/` | The whole project model: scene objects, sequences and tracks, sampling, evaluation (FK + IK), history. No UI. |
| **StudioMenuEx** | `project/src/UI/StudioMenuEx.h/.cpp`, `project/src/UI/Studio/` | The ImGui frontend: windows, keybinds, gizmo, timeline, opening/saving flow. Holds only view state and preferences. |
| **Serialization** | `project/src/Serialization/StudioProjectFormat.h`, `StudioProjectExport.h/.cpp`, `StudioProjectImport.h/.cpp` | The `.nafproject` file format, writing and reading. |

### File map

**Model** (`Animation/Studio/`). `ProjectEx` is the facade; the work happens in the components below it.

| File | Holds |
|---|---|
| `StudioTypes.h` | Ids, refs, enums, `Key`/`Value`/`AnyKey`, modifier settings, info structs, `RigOptions`. `ProjectEx` aliases them, so `ProjectEx::ObjectId` and the like still work. |
| `StudioMath` | Matrix and quaternion helpers (`SlerpShortest`, `RotationBetweenFrames`...), `ToMatrix`/`ToPlacement`, SoA pose access. |
| `Curve` | `Curve`, `Track`: sampling, auto tangents, loop range, key writes. Pure. |
| `Sequence` | `Sequence` and `SequenceSettings`: tracks, cyclic settings, key reads and writes. |
| `SceneObjects` | `Property`, `SceneObject`, `ActorObject` (pose packing, LocalToModel, bone mask), `LocatorObject`. |
| `SharedPose.h` | The double-buffered pose shared with the StudioGenerator. |
| `ObjectStore` | The scene objects in outliner order, with an id index; locator ownership, parent checks. |
| `History` | The change types, commits, transactions, coalescing, saved state. |
| `Evaluator` | Root worlds, the eval plan, the solve, locator resolution (§2.7). |
| `GameBridge` | The only model file that touches the game: actor skeletons and compatibility, `HoldActor`, the StudioGenerator. |
| `Skeleton` | Bone paths, children, the rest pose in model space. |
| `Modifiers/Modifier.h` | `ModifierObject`, the interface every modifier implements, and `ModifierContext`. |
| `Modifiers/IKChain`, `Hand`, `SplineIK`, `CorrectiveDriver` | Each modifier's object, solve, detection and creation (`Build*`). |
| `Modifiers/SplineMath` | Spline IK's curve and bone placement. Pure. |
| `ProjectEx.cpp` | Scene, objects, actors, locators. The rest of the facade is split by feature: `ProjectExHistory`, `ProjectExTimeline` (sampling, transport, sequences, properties, keys), `ProjectExModifiers`, `ProjectExTransforms` (world transforms, bone mask), `ProjectExLoading` (file hooks, rigs), `ProjectExBake`. |

**UI** (`UI/StudioMenuEx` and `UI/Studio/`).

| File | Holds |
|---|---|
| `StudioMenuEx` | The singleton: new, open, close, the dock layout, the camera. Owns the session. |
| `Studio/OpenFlow.cpp` | `StudioMenuEx`'s Binding and Placing steps (§3.1). |
| `Studio/StudioSession` | One open project: what the panels share (selection, interaction, preferences, bone display, clipboard, status), and the panels. Split into `Selection.cpp`, `EditCommands.cpp`, `Actions.cpp` and `EditorSettings.cpp`. |
| `Studio/Actions.h` | The `Action` enum and its labels and keybinds (`GetActionChord`). |
| `Studio/Panels/` | `OptionsBar`, `Outliner`, `Properties`, `Timeline`, `HistoryPanel`, `Viewport`. Each owns its view state. |
| `Studio/Dialogs/` | `Popups` (requests and the small dialogs), `ExportDialog`, `RigDialogs`. |
| `Studio/StudioCamera` | The free camera. |
| `Studio/StudioWidgets` | Colors, names, paths and small widgets shared by the panels. |

Supporting code:
- `UI/DX11Renderer.h/.cpp` projects and draws the viewport overlay.
- `UI/ImguiMenu.cpp` is the F6 menu that creates and opens projects.
- `Serialization/GLTFExport.h/.cpp` writes a baked animation (§2.11) as a `.glb`.

The old `Project`, `Clip` and `StudioMenu` classes are still in the tree and are not used by the new Studio. `StudioMenu::OpenProject(path)` and `SaveProject()` are stubbed out. Leave them alone unless they're being deleted.

---

## 1. Design decisions (agreed, don't relitigate without reason)

**Model and UI split**
- **ProjectEx owns all data and behavior. StudioMenuEx is frontend only.** Auto-key and default interpolation are UI preferences: StudioMenuEx calls `KeyCurrentValue` itself after an edit.

**Properties and sampling**
- **Live values live on the objects.** Every animatable property holds `defaultValue`, `live` and `edited`.
- **Sampling overwrites live values.** It happens on a frame change, a sequence switch, or a key change on that property. Edits that aren't keyed are lost when this happens, but stay undoable: see `DiscardedValue`.
- **Track store, not clips.** Each sequence maps `PropertyRef → Track`. A property is animated in a sequence only while it has a track there. Deleting the last key deletes the track.
- **Switching sequences** resets every property to its default and then samples the new sequence. Untracked properties go back to default.

**Units and naming**
- **Frames are `int32`, frame rate is `float`.** `frameCount` is the sequence duration, so valid frames run `[0, frameCount]`.
- **Keys past the end are kept** when a sequence is shortened. **Moved keys overwrite** the unselected keys they land on.
- **Private members use the `m_` prefix**, parameters `a_`.

**History**
- **History is git-like and linear.** It includes frame changes, binding changes, actor placement and bone mask overrides. It does *not* include playback state, the scene center, or anything in StudioMenuEx (selection, view and bone display).

**Objects and elements**
- **Bones are sub-elements**: `ElementRef{actor, boneIndex}`. Bones can't be renamed, removed, bound or reparented.
- **Modifiers drive bones.** While a chain or spline IK is enabled, gizmo and Properties edits to the bones on its path are rejected (`IsDrivenByModifier`). Additive modifiers (auto-shoulder, finger curl) drive no bones; their offsets are kept out of edits instead (§2.10).
- **The gizmo acts on the whole selection.** With more than one target it's forced to world space.
- **Evaluation order works on bones, not whole actors.** Modifiers (IK chains today) form a dependency graph by which bones they read and write. Cycles are solved iteratively.

**Saving and placement**
- **Nothing is saved in world space.** Actor placements and unparented locators are relative to the **scene center**.
  - The scene center is chosen each time a project is opened.
  - It is not saved, not in history, and **not movable inside the editor.** That's deliberate: moving it would mean recomputing everything that lives in scene space.
- **Actor placement isn't keyable.** It's a translation and heading relative to the scene center, and changes to it are undoable.
- **Opening a project means binding every actor slot first.** Skeletons are only registered after an actor using them has been animated, so the project can't be built from the skeleton map alone.
- **Saved:** everything except history, playback state, actor bindings (only a *binding hint* is kept) and the scene center. UI preferences go in an opaque `"editor"` JSON section owned by StudioMenuEx.

**Keybinds**
- **Keybinds are a `constexpr` table** in `GetActionChord` (`UI/Studio/Actions.cpp`). Every lookup goes through it, so making them configurable only means swapping in a runtime table.

---

## 2. ProjectEx

### 2.1 Addressing

```cpp
ObjectId   = uint32_t     // kNoObject = 0
SequenceId = uint32_t     // kNoSequence = 0
kSelf      = 0xFFFF       // element meaning "the object itself"

PropertyRef { object, element, PropertyId }   // one animatable property
KeyRef      { PropertyRef, frame }            // a track has ≤1 key per frame
ElementRef  { object, element }               // something with a transform
ParentRef   { object, bone }                  // kNoObject = scene space; bone -1 = actor root
ScenePlacement { Float3 translation; float heading; }  // heading: radians about Z, 0 faces +Y
```

`PropertyId` is one of translation, rotation, scale, weight, soften, twist, enabled, shoulderWeight, curl, spread, stretch, tangentLength, orient, twistDistribution.

| Object | Element | Properties | Type |
|---|---|---|---|
| Actor | bone index | translation, rotation, scale | Float3, Quaternion, Float3 (bone local) |
| Locator | `kSelf` | translation, rotation, scale | relative to its parent |
| IK chain | `kSelf` | weight, soften, twist, shoulderWeight, twistDistribution (float), enabled (bool) | |
| Hand | `kSelf` | curl ("Fist"), spread, weight (float), enabled (bool) | |
| Hand | finger index | curl, spread | float |
| Spline IK | `kSelf` | weight, twist, stretch, tangentLength, orient (float), enabled (bool) | |
| Corrective drivers | `kSelf` | weight (float), enabled (bool) | |
| Corrective drivers | driver index | weight | float |

An actor's own placement (`kSelf`) is **not** a property; it's set through `SetActorPlacement` or `SetWorldTransform(kSelf)`.

### 2.2 Value variants

`Value`, `AnyKey`, `AnyCurve` and `PropertyPtr` are variants whose alternatives must stay in the same order: float, bool, Float3, Quaternion. `PropertyPtr` has a leading `monostate`, so its indices are offset by one. A `static_assert` in the constructor enforces the order. Adding a value type means extending all four, plus `kValueTypeNames` and `kValueSizes` in the format header.

`Key<T>` holds `frame`, `interpolation` (Linear/Bezier/Step), `tangentMode` (only `kAuto` today), `value`, `inTangent` and `outTangent`. Bool keys are always Step.

### 2.3 Scene objects

`SceneObject` is the base class: `id`, `name`, `GetType`, `FindProperty(element, property)` and `ForEachProperty(visitor)`.

**ActorObject**
- **Slot requirements:**
  - `skeletonId`, from `Settings::GetSkeletonIdentifier`: the EditorID of the actor's race. An actor fits a slot when `Settings::AreSkeletonsCompatible` accepts its id: the same id, or a race whose skeleton animates the same game bones (`OzzSkeleton::gameSkeletonHash`). On Fallout 4 the saved id can also be the race's behavior graph project, which older projects store.
  - `gender`; `kNone` means any.
- **Binding:**
  - `actor` is a `NiPointer`, null when the slot is unbound.
  - `skeleton` is a `shared_ptr<OzzSkeleton>`.
  - `bindingHint` is the last bound form ID.
- **Placement:** a `ScenePlacement` relative to the scene center.
- **Bones:** `vector<TransformProperties>`, indexed by joint. Their defaults are the skeleton's rest pose.
- **Modifiers:** a `vector<ObjectId>` of IK chains, hands, spline IKs and corrective drivers in application order (§2.10).
- **Bone mask:** `maskOverrides`, a `map<int32_t, bool>` from bone to masked, holding only bones that differ from the skeleton's default mask (§2.12).
- **Evaluation output:**
  - `model` holds the final model-space matrices.
  - `pose` is a `shared_ptr<SharedPose>`, the final local SoA pose, double buffered (see **Pose buffer** in §2.7).
  - `rootWorld` and `hasRoot` describe the actor's world root.

**LocatorObject**
- `transform` is relative to `parent`.
- Its evaluation output is `world`.
- Defaults are the creation placement, so Reset and sequence switches return it there rather than to the parent origin. `AddLocator(parent, world)` takes the placement for this reason; placing a locator afterwards with `SetWorldTransform` would only move its live values.
- Defaults are relative to the parent, like the live values. A reparent that keeps the world transform converts both, measured against the parents' current worlds (`LocatorDefaultsChange`). Keys aren't converted.

**IKChainObject**
- `actor` and the three bones `upperBone`, `middleBone` and `endBone`. Intervening bones are allowed.
- `middleAxis` is computed at creation, in the middle bone's space.
- `effector` and `pole` are locator ids.
- Properties: `weight` (1), `soften` (0.95), `twist` (0), `enabled` (true), `shoulderWeight` (0.5) and `twistDistribution` (1).
- `settings` is an `IKChainSettings`: the auto-shoulder bone and its limits (§2.10).
- Its evaluation output is `reached`.

**IKChainObject**, **HandObject** and **SplineIKObject** derive from `ModifierObject` (§2.10), whose `actor` is the owning actor. `InsertObject`/`RemoveObject` use it to keep `modifiers` in sync.

`m_objects` is in outliner order.

`AddIKChain` creates three objects in one commit, in this order: effector locator, pole locator, then the chain.
- The effector is placed at the end bone, in scene space.
- The pole is placed at the middle bone, offset 20 units along the upper→middle direction and 30 units along the bend normal.

### 2.4 Sequences and tracks

```cpp
Sequence { id, name, frameRate, frameCount, loopSmoothing,
           map<PropertyRef, Track> tracks,
           set<PropertyRef> loopSmoothedProperties,
           map<PropertyRef, BezierMode> bezierModes }
```

`DuplicateSequence` copies the sequence and switches to the copy, in one commit. Duplicating the active sequence also carries over its live values: after the switch, every value that differs from the source's is recorded as a `LiveValueChange`, so unkeyed edits come along as unkeyed edits.

`loopSmoothedProperties` and `bezierModes` are kept separate from `tracks`. Moving or rescaling every key of a track erases the track and writes it again, and that must not lose the settings. `bezierModes` holds only properties that aren't Local.

**Sampling** (`SampleCurve`):
- Before the first key or after the last, the value clamps (unless the curve is cyclic, see below).
- Otherwise it interpolates using the *start* key's interpolation:
  - **Linear:** lerp, or `SlerpShortest` for quaternions.
  - **Step:** the start key's value.
  - **Bezier:** a cubic through `start.value, start.outTangent, end.inTangent, end.value`. Quaternions use De Casteljau with `SlerpShortest`.

> ⚠ Never use `ozz::math::SLerp`. It returns its first argument when the inputs are within ~5° (`|cos| ≥ 0.999`), which turned Bezier rotation into judder. `SlerpShortest` falls back to NLerp at cos ≥ 0.9995 instead.

**Auto tangents** depend on the track's **Bezier mode** (`Sequence::GetBezierMode`, Local unless set). Either way, handles sit at ⅓ of the neighbouring interval.

**Local** (`ComputeAutoTangents`) is Catmull-Rom style: each key's velocity comes from its two neighbours.
- Quaternions work in log space relative to the key.
- Rotational extrema are auto-clamped: the shared velocity is limited to the smaller of the incoming and outgoing speeds. If the motion reverses direction at the key, the velocity is zero.
- End keys use one-sided velocities.
- With uneven key spacing, the longer neighbouring segment dominates. A key placed close to another gets roughly the average speed of the whole span, so keying an in-between changes the curve around it even when the key lies on the curve.

**Continuous** (`RegenerateContinuousTangents`) is a C2 cubic spline: the velocities of every key touching a Bezier segment are solved together (a tridiagonal system), so acceleration also matches on both sides of each key.
- **Keying a frame on the curve doesn't change it.** The old curve still passes through every key and is C2, and the solution is unique. This is exact for floats and Float3, and close for quaternions (about 0.4° in testing).
- The trade-off: moving a key also moves the curve past its neighbours, fading by about 0.27× per key with even spacing. A nudge to a key just after a still one carries forward. In testing, raising a key 3 frames after a still key by 0.01 moved the curve by up to 0.026 about 8 frames later (Local: 0.13).
- **Per component.** Each component is solved separately: floats use x, Float3 uses xyz, and quaternions use the world-space angular velocity in `QuaternionLog` units, which is linear to first order.
- **Holds.** A key holds a component still (velocity 0, which splits the system there) when that component's slope is exactly zero on either side, so equal neighbours stay exactly still. A hold in one component doesn't stop the others.
- **Extremes overshoot.** An extreme key (the slope changes sign) stays free, so acceleration stays continuous through it and the curve may pass the keyed value slightly before turning back. That's 4.4% of the range on a tight 3-key loop, versus 1.1% for Local. Holding extremes was tried first: it made acceleration jump about 4× at every extreme, which shows most at a loop's boundary (usually the rest pose). **To pin an extreme, duplicate its key on the next frame:** the zero slope between the copies holds both. This needs identical values, which a copied key has.
- **Monotonic keys.** Where the value runs one way through a key and the solved velocity points against it (turning the curve back between keys), that key is held and the component solved again.
- **Run ends.** A key with Bezier on one side only continues a Linear neighbour's velocity, or otherwise ends with zero acceleration (natural end). Step segments and open curve ends are natural ends.
- **Cyclic curves** solve the loop range as a cycle, cut at any held key. A loop with no held key (a rotation that keeps turning the same way) uses a cyclic tridiagonal solve (Sherman-Morrison). Keys outside the loop range keep Local tangents, since cyclic sampling never reaches them.
- Keys that touch no Bezier segment keep Local tangents.

`RegenerateTangents` runs after every settings change, including a Bezier mode change. In Local mode, a key write regenerates only the keys whose tangents can change: its neighbours, and the loop range's ends before and after (a range end's tangent depends on the other end). That gives the same result as regenerating the whole curve. In Continuous mode every write re-solves the whole curve, which is O(keys). Bulk edits (moving or rescaling all keys) are quadratic in the length of a Continuous track, which is fine for hand-placed keys.

**Loop smoothing** (called **Cyclic** in the UI; the code and file format keep the old name for compatibility)
- It's on for a property when `sequence.loopSmoothing || loopSmoothedProperties.contains(ref)`.
- A cyclic curve is periodic over `[0, frameCount)`, so keys don't need to sit on the loop boundary. This is what lets offset (overlapping) motion loop without a seam.
- **Loop range** (`GetLoopRange`): the keys in `[0, frameCount]`.
  - Keys past `frameCount` (or before 0) are kept but excluded from cyclic evaluation.
  - A key at `frameCount` is the same point in the loop as frame 0. If frame 0 also has a key, frame 0 wins and the `frameCount` key is ignored.
  - It's *applied* when the range is non-empty and `frameCount > 0`. Otherwise the curve samples as non-cyclic.
- **Sampling:** the frame wraps into `[0, frameCount)`. Before the first key or after the last, it follows the **wrap segment** from the last key to the first key shifted by `+frameCount`, using the last key's interpolation. The segment's length is never zero, because of the frame-0-wins rule. With a single key, the segment is one full loop and the curve is constant.
- **Tangents:** the first key in range gets a ghost previous neighbour (the last key, shifted by `−frameCount`), and the last key gets a ghost next neighbour (the first key, shifted by `+frameCount`). A single key is its own ghost on both sides. Keys outside the range get ordinary tangents.
- Bool tracks wrap too: before the first key, they hold the last key's value.
- `GetLoopSmoothing(ref)` returns `{property, sequence, applied}`.

### 2.5 Transport

- `m_frame` is the integer frame.
- **Playback**
  - `m_playbackTime` is in seconds, and `GetSampleFrame()` is fractional while playing.
  - `Play(loopRange)` stores a `FrameChange` with the edited values it discards.
  - Every tick, `AdvancePlayback` resamples everything, which **bypasses history**.
  - `Pause` rounds to the nearest frame and pushes the stored `FrameChange` as one "Playback" commit.
  - `CancelPlayback` reverts to the start frame and restores the discarded edits. It's used before undo and redo.
- Most mutators call `Pause()` first, inside their transaction.

### 2.6 History

**Changes.** `Change` is a variant, and each alternative has `ApplyChange(T&, bool forward)`.

| Change | Notes |
|---|---|
| `KeyChange{sequence, property, frame, before?, after?, discarded}` | Write or erase one key. In the active sequence it resamples the property. Edits it overwrites are captured on forward and restored on undo. |
| `LiveValueChange{property, before, after, editedBefore}` | Sets `edited = true` going forward. |
| `FrameChange{before, after, discarded}` | Captures edited *tracked* values, sets the frame, then resamples everything. |
| `ActiveSequenceChange{before, after, frameBefore, frameAfter, discarded}` | Captures *all* live values, resets to defaults, then resamples. |
| `ObjectLifetimeChange{ObjectSnapshot, existsAfter}` | The snapshot owns the object, its tracks in every sequence, its loop-smoothing and Bezier mode entries, and its index and modifier index. |
| `SequenceLifetimeChange{id, sequence, index, existsAfter}` | |
| `SequenceSettingsChange{id, before, after}` | Name, frame rate, frame count, loop smoothing (cyclic). Regenerates tangents and resamples unedited properties. |
| `ObjectRenameChange`, `LocatorParentChange`, `LoopSmoothingChange` | |
| `BezierModeChange{sequence, property, before, after}` | Regenerates the track's tangents and resamples it if unedited. `SetBezierMode` records one per property whose mode changes, so undoing a batch restores mixed modes. Bool properties are skipped. |
| `LocatorDefaultsChange{id, before, after}` | A locator's default translation, rotation and scale. Recorded by a reparent that keeps the world transform, and by `SetLocatorDefaultsToCurrent`. |
| `ActorBindingChange{id, before, after}` | Detaches and reattaches the StudioGenerator, re-initializes the pose if the skeleton changed, and updates `bindingHint`. |
| `ActorPlacementChange{id, before, after}` | `Update()` applies it next frame. |
| `ModifierSettingsChange{id, before, after}` | A `ModifierSettings` variant (`IKChainSettings`, `HandSettings`, `SplineIKSettings`). Marks the eval plan dirty. |
| `BoneMaskChange{id, before, after}` | An actor's whole `maskOverrides` map. Publishes the mask (§2.12). |

**Transactions**

The bookkeeping is in `History`; applying a change stays in `ProjectEx` (`ProjectExHistory.cpp`).
- `BeginEdit`, `EndEdit` and `CancelEdit` (internally `TransactionScope`) nest; only the outermost scope commits.
- **Every public mutator is its own commit** unless wrapped in an outer edit.
- `Record(change)` applies the change and pushes it. `History::Push` alone pushes a change that's already applied (used by Pause).
- **Coalescing** (`History::TryCoalesce`):
  - A FrameChange merges only into an *immediately* preceding FrameChange.
  - Live and key changes merge into the latest change on the same target, scanning back past changes to other properties, and stopping at anything else.
  - The open transaction keeps an index (`OpenIndex`) of its latest live change per property, key change per property and per (sequence, property, frame), and latest other change. It finds the same change the scan would, without scanning: bulk key edits record thousands of changes in one commit.
- **No-op changes are dropped**, and an empty commit isn't recorded.
- `CancelEdit` reverts the open transaction but keeps the scopes open. UI drags use this to rebuild the operation from its start state each frame.
- **Limits and saved state**
  - At most `m_maxCommits` = 256 commits are kept.
  - `m_savedHead` tracks the saved point and is cleared if it's trimmed away or branched off from.
  - `HasUnsavedChanges()` returns `!m_savedHead || *m_savedHead != m_head`.

### 2.7 Evaluation

The steps below are `Evaluator`'s, except `Update` and playback. `ProjectEx` marks it dirty through `MarkSolveNeeded`, `MarkRootsDirty` and `MarkPlanDirty`.

`Update(dt)` runs once per Present, at the end of StudioMenuEx::Draw:
1. **Holds every bound actor** at `sceneCenter * placement` with `HoldActor`. This sets `Graph::rootTransform`, `kLockHeading` and the position lock, and runs every frame, because the game can reset a graph's root, for example on a cell change.
   - The lock needs no explicit release. It lives on the actor's NAF graph, and that graph is destroyed once its generator is detached (remove, unbind, rebind, project closed), unless the graph has `kPersistent`.
   - If a persistent graph is ever involved, the lock would outlive the project and would have to be cleared by hand.
   - `HoldActor` uses `VisitGraph` without creating a graph, so it never creates one just to hold an actor. Restoring the actors' original placements when the Placing step is cancelled relies on this: it holds them during the detach transition, and the graph then goes away.
2. Advances playback.
3. Sets `m_rootsDirty`, then calls `Evaluate()`.

Drawing happens before `Update`, so the overlays show the pose that's on screen; playback and any edits made while drawing reach the pose buffer before the game's next update reads it.

`Evaluate()` is also called by the getters (`GetWorldTransform`, `GetModelTransforms`, `GetSplineIKCurve`), many times per frame. It returns early when nothing has changed:
1. **Root worlds**, only when `m_rootsDirty` (set once per `Update`, since the game can unload or rescale an actor's 3D) or `m_solveNeeded` (every change that moves a root's inputs, such as placement, scene center or binding, goes through `Apply` and sets it). `RefreshRootWorld` sets `rootWorld` to `ToMatrix(sceneCenter * placement)`, the placement the actor is held at, with only the scale taken from `Get3D()->world`. The pose solved now is applied at the game's next update, which is also when the 3D reaches the held placement, so solving against the 3D as it stands would put world-space targets off by however far the actor just moved. A change sets `m_solveNeeded`.
2. **Eval plan.** If `m_planDirty`, `BuildPlan()` runs:
   - `DescribeModifier` gives a modifier's write roots (`ModifierObject::GetWriteRoots`) and its reads. For a chain, the write root is its upper bone (or its shoulder, when it has one) and all descendants. The reads are each write root's parent, the bones the modifier names in `AppendBoneReads`, and the bone each of its locators (`GetLocators`) ultimately hangs from. Spline IK works the same way with its start bone and controls. Corrective drivers have several write roots (twist bones and rib helpers) and read the bones that drive them. Hands have no write root, so they aren't in the plan.
   - B depends on A when B reads a bone A writes (`Affects`).
   - Tarjan SCC finds the groups; steps run in topological order; a cycle group runs `kCycleIterations` (4) times.
3. **Solve** (`Solve(publish)`). If `m_solveNeeded`:
   - For every actor, `PackPose` (live bone values → SoA pose buffer), each modifier's `ApplyToPose` (hands), and then a full `LocalToModelJob`.
   - Run the plan: each modifier's `Solve`, with the `Evaluator` as its `ModifierContext` (§2.10). `IKChainObject::Solve`:
     - Swing the shoulder, if the chain has one (`SolveShoulder`).
     - Run `IKTwoBoneJob` in model space.
     - Apply the two corrections to the pose, then `LocalToModel` from the upper bone.
     - Spread the end bone's twist over the bones above it (`DistributeTwist`).
     - Align the end bone's rotation to the effector by weight, then `LocalToModel` from the end bone.
   - Publish every pose buffer (`Swap`), unless baking (§2.11).
   - Resolve every locator's world matrix, recursively through its parents. Parent loops are rejected at parenting time.

**Pose buffer**
- The pose buffer is shared with a `StudioGenerator` attached to the actor's NAF graph (`GraphManager::AttachGenerator`, transition time 0).
- Each graph update it copies the buffer into the graph's output. It also carries the actor's bone mask (§2.12).
- It's double buffered (`SharedPose`). Evaluation writes the back buffer on the Studio's thread and publishes it with `Swap` once every modifier has run, so the generator, on the game's animation thread, never reads a partly solved pose. Reads that need the published pose, such as the additive offset in `SetWorldTransform`, use `Front()`.
- It's detached with a 0.2s transition: on remove, rebind, or the project being destroyed.
- IK corrections live only in the pose buffer and the model matrices, never in the bones' live values, so they don't accumulate.

**Constraints enforced on edit:**
- `SetLocatorParent` rejects loops (`WouldCreateLocatorLoop`) and parents that make a chain or spline read its own output (`HasSelfDependentModifier`). `SetIKChainSettings` rejects a shoulder for the same reason.
- `RemoveObject` cascades: an actor takes its modifiers, a chain takes its effector and pole, and a spline takes its controls. Removing a locator removes the chain or spline that uses it (`GetLocatorOwner`). Surviving locators parented to a removed object are reparented to scene space, keeping their world transform.
- `BindActor` checks that the skeleton ids are compatible, the gender, and that the joints match the slot's, in the same order.

### 2.8 Scene center and placement

```cpp
static Float4x4       ToMatrix(ScenePlacement);      // heading h: +Y → (sin h, cos h)
static ScenePlacement ToPlacement(Float4x4);         // keeps translation + heading only
static ScenePlacement GetWorldPlacement(TESObjectREFR*);  // data.location, data.angle.z
static void           HoldActor(Actor*, ScenePlacement world);
```

- `ResolveParentWorld(kNoObject)` returns `ToMatrix(m_sceneCenter)`, so unparented locators are in scene space.
- `SetSceneCenter` isn't recorded or saved. It's meant to be called only while a project opens.
- `AddActor` sets placement = `inverse(center) * actorWorld`. If the new actor has id 1 (the project's first object ever), it first moves the center onto that actor, so its placement is zero.
- `SetWorldTransform({actor, kSelf}, world, mask)` converts to a placement and records an `ActorPlacementChange`. The mask picks translation and/or heading.

**Setting several world transforms.** `SetWorldTransforms(span<WorldTransformEdit{element, world}>, mask)` applies edits in order, so parents go before children; `SetWorldTransform` is the one-edit case. Each edit is measured against the evaluated transforms, so it evaluates once up front, and between edits only when `IsAffectedByEdits` says the edits since the last evaluation may have moved what the next one reads. That's when:
- the target's actor (or the actor a target locator ultimately hangs from) had its placement edited,
- an edited bone is an ancestor-or-self of the target bone, or of the bone a target locator hangs from,
- or that bone lies under a write root of one of the actor's model-space modifiers (`DescribeModifier`). Any edit counts as changing a modifier's result, since modifiers can read locators and other actors.

Actor placements read nothing evaluated and never trigger it. Locator chains resolve from live values, which are always current. A full evaluation per edit made multi-selection gizmo drags scale with the selection size.

### 2.9 Loading hooks (friends: StudioProjectExport, StudioProjectImport)

- `LoadActorBinding(slot, actor)` checks compatibility, gets or creates the actor's graph skeleton (`GetActorSkeleton`, `VisitGraph` with create), sets `actor` and `bindingHint`, and resets the bones to the rest pose.
- `FinishLoad(liveValues)`:
  1. Regenerates the tangents of every sequence.
  2. Clamps the frame and resets to defaults.
  3. Resamples the active sequence.
  4. Applies the saved live values (type-checked).
  5. Attaches StudioGenerators, clears history, marks the eval plan dirty, and calls `MarkSaved()`.
- `IsValidChain`, `WouldCreateLocatorLoop` and `HasSelfDependentModifier` are reused by the importer.

### 2.10 Modifiers: auto-shoulder, finger controls, spline IK

An actor's `modifiers` list holds every object that changes its pose after FK: IK chains, hands, spline IK and corrective drivers. Each derives from `ModifierObject` (`Modifiers/Modifier.h`), and the rest of the model only uses that interface:

| Member | Meaning |
|---|---|
| `actor` | The owning actor. |
| `GetLocators`, `UsesLocator` | The locators it owns: removed with it, and removing one removes it. |
| `Remap(actor, locatorMap)` | Points it at another actor and new locator ids (applying a rig). |
| `ApplyToPose(actor)` | Pose-time: adjusts the local pose before LocalToModel (hands). |
| `GetWriteRoots(skeleton)`, `AppendBoneReads`, `Solve(context)` | Model-space: the bones it writes with all their descendants, any bones it reads besides their parents and its locators, and its solve in the eval plan (IK chains, spline IK, corrective drivers). No write roots keeps it out of the plan. |
| `DrivesBone(parents, bone)` | Bones it fully owns while enabled. |
| `GetSettings`, `SetSettings` | Its non-keyable settings, as a `ModifierSettings` variant. |

Each modifier's file (`Modifiers/IKChain`, `Hand`, `SplineIK`) also holds its detection, validation and creation (`BuildIKChain`, `BuildHand`, `BuildSplineIK`, which return the locators and the modifier for `ProjectEx` to record).

All of them follow the same rules:
- **Corrections live only in the pose buffer and the model matrices**, never in live values, so they don't accumulate.
- **`DrivesBone`** tells which bones a modifier fully owns. `IsDrivenByModifier` (formerly `IsDrivenByIK`) rejects gizmo and Properties edits to those bones; `GetDrivenBones(actor)` answers for every bone at once. Additive modifiers (finger curl, auto-shoulder) own no bones, so the bones they touch stay editable.
- **Editing a bone with an additive offset.** `SetWorldTransform` measures the bone's offset as `offset = inverse(live) * final`, where `final` is the local rotation in the pose buffer. It then stores `live = desired * inverse(offset)`, so the offset isn't baked into the keys. A translation offset (corrective drivers slide bones) is kept out the same way: `live = desired - (final - live)`. For finger curl this is exact. For the shoulder and corrective drivers, the offset is recomputed after the edit, so dragging is close to 1:1 but not exact.
- **Settings that aren't keyable** (shoulder bone and limits, finger setup, spline options) change through one `ModifierSettingsChange{id, before, after}`. It holds a `ModifierSettings` variant: `IKChainSettings`, `HandSettings`, `SplineIKSettings` or `CorrectiveDriverSettings`. Applying it marks the eval plan dirty, since a modifier's write root can change.

**Evaluation order:**
1. `PackPose`.
2. **Hands** (pose-time drivers, not in the eval plan).
3. The full `LocalToModelJob`.
4. The eval plan (IK chains and spline IK).
5. Locators.

#### Auto-shoulder (IK chain)

`IKChainSettings{ shoulderBone = -1, shoulderMaxAngle = 30°, shoulderNeutral }`, plus the keyable float `shoulderWeight` (default 0.5), the influence.

This is the Blender "damped track to a dummy" setup: a dummy point sits an arm's length out along the clavicle and moves with the IK target, and the clavicle aims at it. The shoulder follows every move of the effector away from a relaxed position, a little and in proportion, rather than reacting to how far the target is (versions 4–5 did that, and started early, did little when needed and stopped dead).

**Detection.** `AddIKChain` sets `shoulderBone` to the upper bone's parent P when all of these hold:
- P isn't a root.
- The upper bone's subtree is P's largest child subtree.

A clavicle qualifies. A pelvis doesn't, because its spine subtree is larger than the thigh's. Outliner → IK chain → **Shoulder ▸** sets any strict ancestor of the upper bone, or clears it.

**Neutral.** `shoulderNeutral` is where the effector counts as relaxed, in the space of the shoulder's parent (the chest), so moving the torso doesn't count as moving the hand. It's captured from the effector (`ToShoulderNeutral`) when the chain is built (the effector starts on the end bone), whenever `SetIKChainSettings` picks a different shoulder bone, and by **Set Neutral** (`SetIKChainShoulderNeutral`, one undo step). Files without one use the end bone's rest position (`GetRestShoulderNeutral`).

**Solve.** It runs in `IKChainObject::Solve`, in model space, before the two-bone job:
1. Take `c` = the shoulder position, `u` = the upper bone position, `t` = the effector position, `n` = the neutral point through the shoulder's parent's model matrix, `L = |mid − upper| + |end − mid|` and `aim = normalize(u − c)`.
2. The dummy is `D = c + aim · (|u − c| + L) + (t − n)`. `aim` comes from the shoulder's current pose, not a captured one: with the effector at neutral, `D` lies on the shoulder's own line, nothing moves, and the shoulder's keys pass through (it stays additive).
3. The swing is the shortest arc from `aim` to `normalize(D − c)`, its angle times `shoulderWeight · weight`, through `SoftLimit(angle, shoulderMaxAngle)`: linear up to half the limit, then easing exponentially toward it with a continuous slope.
4. The local correction is `inverse(Rc) · q · Rc`, where `Rc` is the shoulder's model rotation. Post-multiply it into the pose, then `RunLocalToModel(shoulder)`.
5. The existing two-bone solve runs as before. It reads the upper bone's new position and recomputes the pole vector.

A hand move of Δ across the clavicle's line turns it by about `atan(Δ / (clavicle + limb length))` times the influence. Moving the hand along that line doesn't turn it at all.

**Guide.** The solve keeps `shoulderGuide` (shoulder, neutral and dummy, model space); `GetIKChain` returns it in world space, nullopt for a chain without a shoulder or that didn't solve. While the chain or its effector is selected, the viewport draws the neutral and dummy points, a line from neutral to the effector, and one from the shoulder to the dummy.

Version 6 replaced the reach-based solve of versions 4–5. Older files keep their shoulder bone and max angle, read `shoulderWeight` as the default, and get the rest-pose neutral.

`DescribeModifier`: with a shoulder set, the write root is the shoulder and the read is its parent.

#### Twist distribution (IK chain)

The keyable float `twistDistribution` (0–1, default 1). The end-bone alignment turns the end bone to the effector's rotation, so an effector twisted about the lower limb candy-wraps the forearm if only the hand turns. `DistributeTwist` runs just before the alignment:
1. The lower limb axis `a` runs from the middle bone to the end bone. Take `delta = targetModel · inverse(endModel)`, and its twist about `a` (swing-twist decomposition), wrapped to ±180°, times `twistDistribution · weight`.
2. Each bone strictly between the middle and end bones (`GetBonePath`) takes the twist up to `along`, its position projected onto the lower limb (0 at the middle bone, 1 at the end bone, clamped), less what its parents already gave it. An elbow-side bone gets almost none and a wrist-side bone almost all. On the FO4 human skeleton that's `ForeArm_OFFSET`, `ForeArm2`, `ForeArm3` and `Hand_OFFSET` between `ForeArm1` and `Hand`.
3. Each bone's turn is about the line from its own position to the end bone, so the end bone doesn't move. It's applied like the shoulder swing: `local · (R⁻¹ · q · R)` with `R` the bone's model rotation before any of the turns. Down the chain, the model rotations compose to `q₁ ⋯ qₖ · R`.
4. The end bone's local rotation is pre-multiplied by `P⁻¹ · (q₁ ⋯ qₙ)⁻¹ · P` (`P` = its parent's model rotation before the turns), keeping its model rotation, so the alignment's weight blend starts from the same place as without distribution. Then one `RunLocalToModel` from the first bone between.

A chain with no bones between its middle and end bones has nothing to spread over, and behaves as before. Twist bones that hang off the limb rather than on the path (FO4's `UpperTwist1/2` off `UpperArm`) aren't touched.

`DescribeModifier`: with a shoulder set, the write root is the shoulder and the read is its parent.

#### Hands (finger controls)

`HandObject{ actor, handBone, HandSettings{ fingers } }`.

Each `FingerSetup` holds:
- `bones`, from base to tip,
- `curlAxes` (one per bone, in that bone's local space),
- `spreadAxis` and `spreadScale` (in the base bone's local space),
- `maxCurl` (radians per bone),
- `maxSpread` (default 15°),
- `flip`,
- `straighten` (radians per bone, about its curl axis): the turn from the rest pose to straight, so **curl 0 is a flat finger**,
- `maxExtend` (radians per bone): how far curl −1 bends back past straight.

`straighten` and `maxExtend` were added in version 5. Hands saved before that have neither, and keep the old meaning: curl 0 is the rest pose, and `maxCurl` applies in both directions.

**Properties** (addressed by `element`):

| Element | Property | Default | Meaning |
|---|---|---|---|
| `kSelf` | `curl` | 0 | "Fist": added to every finger's curl |
| `kSelf` | `spread` | 0 | added to every finger's spread |
| `kSelf` | `weight`, `enabled` | 1, true | |
| finger *i* | `curl` | Relaxed preset | effective curl clamped to [−1, 1]: 0 is straight, 1 is `maxCurl`, −1 is `maxExtend` past straight |
| finger *i* | `spread` | 0 | effective spread clamped to [−1, 1] |

A new hand's finger curls default to the Relaxed preset (by role, as below), because curl 0 is flat and would visibly change the hand.

**Detection** (`AddHand`, from the skeleton's rest pose in model space):
- **Fingers.** Each child of the hand bone starts a finger, followed down while a joint has exactly one child. Chains shorter than two joints are dropped, which removes weapon and attach nodes. If any remaining chain's name contains "finger" or "thumb", only those are kept.
- **Thumb.** With three or more fingers, the thumb is the finger whose base direction from the hand deviates most from the mean of all base directions. It's listed first. The others are ordered by base distance from the thumb, so they read index → pinky.
- **Palm normal.**
  - `n = normalize(cross(index − H, pinky − H))`, where H is the hand position.
  - It's negated when H is on the actor's −X side (the actor's left, since actors face +Y).
  - It orients spread, defines the plane a straight finger's base lies in, and estimates curl for fingers that are straight at rest.
- **Curl axis per bone: the skeleton's own hinges.**
  - A finger joint's rest rotation (relative to its parent) is a bend about its hinge, so that rotation's axis is the curl axis, whatever the hand's orientation. It's read for every joint after the base whose rest bend is at least 8°.
  - It's signed so the rest bend counts as curl. All joints of a finger are made to agree with its most-bent joint, so a joint slightly hyperextended at rest doesn't curl backward.
  - Joints without a measurable bend (always the base, which turns relative to the hand rather than hinging) use the nearest joint's hinge, so the whole finger curls in one plane.
  - **Fallback**, for a finger that's straight at rest: estimate `cross(boneDir, n)`, then snap it to the bone's nearest local axis across the bone, which is how skeletons are normally authored.
  - Checked against `skeleton_fo4.nif`: every joint of both hands, thumb included, has a pure rest rotation about local −Z, matching its bend (dot = 1.000). An earlier version used `cross(boneDir, n)` for every finger; that was ~73° off on the thumb, and wrong on the ring and pinky in game.
- **Spread axis:** `cross(boneDir, curlAxis)` in the base bone's space, signed to agree with `n`.
  - `spreadScale` is the signed, normalized distance of the finger's base from the middle of the non-thumb fingers. So positive spread fans the fingers apart, and the middle finger barely moves.
  - The thumb uses +1, which abducts it.
- **`straighten`: what "straight" means, from bone positions alone.** Each bone lies in line with the one before it, and the base bone lies in the palm's plane. Every angle is measured about the joint's own curl axis (`AngleAbout`: both directions projected across the axis, then a signed `atan2`), so straightening stays in the plane the finger curls in, and curl passes back through the rest pose.
  - Joints after the base: the angle that turns the bone's direction (toward its child) onto its parent bone's direction.
  - The tip has no child, so its direction is its parent bone's local axis carried into its own frame: `rest[tip] · restLocal[tip].translation`. With an identity local rotation, that's exactly in line.
  - The base: the angle that turns the base bone's direction until it has no component along `n` (with `n` first projected across the curl axis). Only the curl-plane part changes, so the finger's natural splay is kept. A curl axis nearly parallel to `n` gives 0.
  - **The thumb's base keeps its rest angle (0)**: its CMC joint sits out of the palm plane in any skeleton. Its other joints straighten as usual.
  - Clamped to ±90°.
- **Default `maxCurl`:** 90°, 100° and 70° for base, middle and tip bones (the last value repeats for longer fingers). The thumb uses 35°, 45° and 60°. All from straight. (Before version 5 the rest bend was subtracted, so Fist at 1 ends in about the same place either way.)
- **Default `maxExtend`:** 20°, 5° and 10°. The thumb uses 5°, 10° and 20°.

**Applying it** (after `PackPose`, for each enabled hand with weight > 0):
- `curl = clamp(fist + curl_i, −1, 1)`, `sign = flip ? −1 : 1`.
- `spread = clamp(spreadAll + spread_i, −1, 1) · weight`.
- Curl angle per bone *j*: `weight · (straighten[j] + sign · curl · (curl ≥ 0 ? maxCurl[j] : maxExtend[j]))`. The range is picked from the unflipped curl. Flip doesn't change `straighten`, since that's measured geometrically about the stored axis.
  - Before version 5 (no `straighten`): `weight · sign · curl · maxCurl[j]`.
- Base bone: `local = local · AxisAngle(spreadAxis, spread · maxSpread · spreadScale) · AxisAngle(curlAxis, curlAngle[0])`.
- Other bones: `local = local · AxisAngle(curlAxis_j, curlAngle[j])`.
- Weight 0, or a disabled hand, leaves the skeleton's own rest hand.
- Being additive and before LocalToModel, IK end-bone alignment and fingertip-parented locators see the curled hand without an eval-plan entry.

**UI**
- **Create:** **Create Finger Controls** on a single selected bone, which is the hand.
- **Properties panel:**
  - Enabled, Weight, Fist and Spread, then one row per finger with Curl and Spread.
  - **Presets** (Relaxed, Fist, Point, Grip, Flat) set every finger's curl by role (thumb, then index to pinky), and reset Fist and every Spread to 0. They're keyed when Auto Key is on. A hand with fewer than three fingers has no thumb, so its first finger takes the index role.
  - A **Setup** section has, per finger, Flip, then Max Curl, Max Extend and Straighten per bone, and Max Spread. Straighten is there for skeletons where detection gets "straight" wrong. Hands from before version 5 show only Max Curl, with a note to recreate them.
- **Context menu:** **Copy Pose To ▸** copies the values to another hand on the same actor with the same finger count.
- **Outliner:** a hand is a leaf under its actor.
- **Timeline:** finger lanes are labelled "<first bone> Curl".

#### Spline IK

`SplineIKObject{ actor, startBone, endBone, bones (path, start → end), aimAxes, upAxes, controls, controlAxes, SplineIKSettings{ rootFollowsCurve = false, alignTip = true } }`.

**Data**
- `aimAxes[j]` is the direction from bone j to the next bone on the path that's apart from it, in bone j's local space. The tip, and any bones on top of it, use the direction from the last earlier bone that's apart.
- **Coincident bones.** Bones closer together than 1% of the chain's length (at least 0.0001 units) share one point on the curve. An example is zero-length `_OFFSET` bones between the bones that shape a chain. `GetSplineAimSegments` makes each such bone aim past the shared point, both when the axes are computed and in every solve. So the bones turn as one and keep their rest relationship. Aiming at a coincident neighbour gave a near-zero direction, which turned the bone to an arbitrary rotation, and through the end controls' aim axes it bent the curve's end tangents.
- `upAxes[j]` is calibrated at creation (see below).
- `controls` are N ≥ 2 locator ids. `controlAxes[k] = {aim, up}` are in the control locator's local space.
- Keyable properties: `weight` (1), `twist` (0, radians), `stretch` (0–1, default 0), `tangentLength` (1), `orient` (0–1, default 1) and `enabled`.

**Curve.** Centripetal Catmull-Rom through the control positions in model space (α = 0.5), evaluated as cubic Hermite segments.
- **Interior tangent at P_i:** `m_i = (P_i − P_{i−1})/d0 − (P_{i+1} − P_{i−1})/(d0 + d1) + (P_{i+1} − P_i)/d1`, where `d = |ΔP|^0.5`. A segment uses `m · d_segment`.
- **End tangents:**
  - The first control's aim direction × the first chord length × `tangentLength`.
  - The same for the last control, using the last chord.
  - So rotating an end control bends the chain near it, and two controls already make a usable S-curve.
- **Arc-length table:** 16 samples per segment.

**Solve** (`SplineIKObject::Solve`, model space):
1. **Root.**
   - If `rootFollowsCurve` is off, the first bone stays at its FK position. The curve is offset by `(fk0 − C(0)) · (1 − s/L)`, so the offset fades out and the tip still reaches the last control.
   - If it's on, the first bone sits at `C(0)`.
2. **Placement.** Each next bone is the first point down the table whose **chord** distance from the previous bone equals the current FK bone length, solved exactly on that table segment. This keeps bone lengths exact. If the curve runs out, bones continue along the end tangent.
3. **Stretch.** Positions blend toward `C(L · restOffset_j / chainLength)`, which places bones proportionally along the curve. The bones' local translations are scaled by the resulting length ratio.
4. **Frames.**
   - A rotation-minimising frame (double reflection, Wang et al. 2008) carries the first control's up vector along the table.
   - At each later control, the roll from the transported up to that control's up (projected ⊥ tangent) is measured. Roll is interpolated linearly by arc length between controls.
   - `twist · s/L` is added on top, so the tip turns by `twist` and the root doesn't.
5. **Orientation.**
   - Bone j's model rotation maps `aimAxes[j]` to `normalize(p_{j+1} − p_j)` and `upAxes[j]` to the frame's up at its arc position.
   - The tip: with `alignTip`, it takes the last control's rotation, the same as the IK end-bone alignment. Otherwise it aims along the end tangent.
6. **Write.** Bone by bone from the start, each against its parent's new model rotation and position:
   - **Translation** (not the first bone). Start from the pose's local translation, scaled by stretch. When `orient < 1`, blend it by `1 − orient` toward the offset that puts the bone at `FK + (solved − FK) · weight`, measured in the parent's new frame and divided by the parent's FK scale.
   - **Rotation.** Blend the model rotation with FK by `weight · orient` (`SlerpShortest`), convert it to local, and write it to the pose.

   Then `RunLocalToModel(startBone)`.
   - **Orient** decides how the bones reach the curve. At 1 they turn and keep their lengths, the original behaviour, which leaves translations untouched apart from stretch. At 0 they keep their FK rotations and are moved onto the curve, so their lengths stretch.
   - Turning far from the rest pose collapses volume on meshes skinned for physics-driven bones, such as breasts, which are made to be moved rather than rotated. Low Orient avoids that.
   - Align Tip is a rotation, so it's scaled by Orient too.
   > ⚠ Pose translations are written with `SetPoseTranslation`, not `Util::Ozz::ApplySoATransformTranslation`. That helper transposes four rows starting at `translation.x`, so its fourth row is `rotation.x`, and the new vector's w overwrites one lane of it.

**Creation** (`AddSplineIK(actor, startBone, endBone, controlCount)`):
- The path must have at least 3 bones, and `2 ≤ controlCount ≤` the number of distinct points on it (`GetSplineIKControlLimit`). Coincident bones count as one point.
- Controls go in scene space **on bones** spread evenly by arc length along the current pose. Each takes that bone's world transform.
  - The candidates (`GetSplineControlCandidates`) are one bone per distinct point: the first bone of each point, except that the end bone stands for the last one. So no two controls share a position; that would make the interior tangent divide by about zero.
  - The first control always goes on the start bone and the last on the end bone, so the tip alignment uses the end bone's own rotation.
- `controlAxes`: the bone's aim axis, and the local basis axis least parallel to it as up.
- **Up calibration.** Steps 1–4 run once on the creation pose, and `upAxes[j] = inverse(R_fk_j) · up_j`. So with untouched controls, roll matches the FK pose.
- **Expected drift:** bones between controls can move slightly at creation, because the curve isn't the FK polyline. More controls reduce this.

**Rules**
- `DescribeModifier`: the write root is the start bone. The reads are the start bone's parent, plus every control's bone (`AppendLocatorReads`).
- `DrivesBone`: the path.
- Removing a control removes the spline, and removing the spline removes its controls.

**UI**
- **Create:** **Create Spline IK** from exactly two bones of one actor, one an ancestor of the other. It opens a dialog for the number of controls: 2 up to one per distinct point on the path, 3 by default. The count is fixed once the spline exists.
- **Outliner:** lists the controls under the spline.
- **Viewport:** controls are orange, and the curve is drawn as a polyline with the IK lines. A disabled spline (or one with weight 0) isn't solved, so it has no curve to draw.
- **Properties panel:** Enabled, Weight, Twist, Stretch, Tangent Length, Orient, Root Follows Curve, Align Tip, and the control list.

#### Corrective drivers

`CorrectiveDriverObject{ actor, CorrectiveDriverSettings{ drivers } }`: drives a skeleton's corrective (helper) bones from the bones around them, the way the game's own animations move them. Studio otherwise leaves these bones wherever the pose put them, which is what makes Fallout 4's shoulders pinch and armpits stretch when an arm is posed by hand or by IK. An actor has at most one (`AddCorrectiveDriver` and `ApplyRig` refuse a second).

**Fallout 4 only, so far.** Creation (`BuildCorrectiveDriver`) finds the human skeleton's bones by name under `#ifdef TARGET_GAME_F4`; elsewhere it fails, and the UI hides the action. The object, its file format and its UI are shared, so Starfield's corrective bones can be added as new kinds.

**Drivers.** `CorrectiveDriver{ kind, mirrored, bones }`, one per kind and side, solved in list order (twists before the armpit, which swings the twist bones as the twist driver left them):

| Kind | Bones (`GetCorrectiveDriverBoneCount`) | Moves | From |
|---|---|---|---|
| `kArmTwist` | upper arm, twist 1, twist 2 | twist 1 and 2 about the upper arm's long axis | the upper arm's twist from rest |
| `kArmpit` | chest, upper arm, twist 1, twist 2 | twist 1 and 2: a swing and a slide | the arm's raise above its rest elevation |
| `kRibHelper` | chest, collarbone, rib helper | the rib helper: a turn about its local Y and a slide up the chest | the collarbone's raise |

On the F4 human skeleton those are `LArm_UpperArm`, `LArm_UpperTwist1/2`, `LArm_Collarbone`, `L_RibHelper` and `Chest`, and the `R` versions (`mirrored`). The Havok rig animates the twist bones and rib helpers (vanilla animations carry the helper motion baked in from the authoring rig); the `_skin` bones the body is weighted to hang under them.

**Properties.** `enabled` and `weight` (0–1) on `kSelf`, and each driver's `weight` (0–2, default 1) as element *i*. A driver's strength is `weight · driver weight`, applied to its angles and slides.

**Rules** (constants in `CorrectiveDriver.cpp`). They came from offline analysis of the game data, not from the game: every vanilla human animation was decoded and sampled (about 89k frames), and a body mesh (TWB-3BBB) was skinned on the skeleton to measure triangle stretch.
- **Arm twist** (vanilla's rule): with `t` the upper arm's twist from rest about its long axis (toward twist 2), twist 1 turns by `−0.92·t` and twist 2, relative to twist 1, by `+0.46·t`. So the shoulder end of the upper arm stays untwisted, mid-bicep takes about half and the elbow end all of it. In vanilla data, twist 1 vs the upper arm's twist fits at −0.92 (r = −0.96), and twist 2 is exactly −0.5 × twist 1. Each turn is pre-multiplied (in the parent's space).
- **Armpit** (fitted, not in vanilla): elevation is the arm's angle from straight down the spine (toward the chest's parent), in the chest's space; `d` is how far it's above the rest elevation, in degrees. Twist 1 swings back toward down, about `cross(arm, down)`, by `0.8·d / (1 + d/53.75)` degrees, and twist 2 swings forward by 0.944 of that, so the bicep keeps its direction while the armpit skin it carries stays lower. Both slide toward the top of the arm (perpendicular to it, away from down): twist 1 by `0.04·d` units and twist 2 by a further `0.027·d`. Twist 1 also slides back along the arm, toward the shoulder, by `0.015·d`, and the bicep moves with it. The slides come from a hand correction of a held-overhead pose (about 83° of raise), turned into a per-degree rule and checked by rendering at other raises; vanilla never slides the twist bones (all but three broken clips hold them at rest). They open the armpit's front skin, which linear blend skinning otherwise crushes (squeezed to a few percent of its width while stretched over 10× the other way, which smears the UVs), and let the top of the shoulder compress into a fold instead, which reads as volume. The stretch-only fit missed this, because it counted that compression as heavily as stretch; on its average a slide toward the top of the arm looks worse. Straight overhead the swing plane is undefined, so it falls back to the rest arm's plane. It's geometric, so it mirrors without `mirrored`.
  - Why: the armpit vertices are weighted 30–60% to twist 2's skin bone, which sits mid-bicep, against the ribcage. When the arm swings far, linear blend skinning averages a point carried with the arm against one on the chest, which stretches the band between them. The fit minimizes stretch over 24 arm poses (raise 70–160°, sideways to forward, collarbone at rest or raised) with a penalty on moving the bicep off the arm. It cuts stretch by 16% on average and up to about half for sideways raises, with bicep vertices moving 0.76 units RMS (1.6 at most). Without the penalty it cuts more (22%) but visibly bends the upper arm. A higher driver weight trades the same way.
- **Rib helper** (vanilla's rule, made symmetric): `r` is the collarbone's turn from rest about its local Y (−Y on the right). The rib helper turns about its local Y (−Y on the right) by `min(35°, 0.9·(r − 5°))` and slides up the chest by 0.087 units per degree. Vanilla's left side follows 0.7× with no cap and its right side 1.1× capped at 35°; the slide is 0.087 per degree on both. Its effect on stretch is small: the body weights it lightly.
- Raising the collarbone makes armpit stretch worse (the shoulder lifts away from skin weighted to the ribcage), so these drivers don't move it.

**Solve.** In the eval plan, after the IK chains that move the arms (it reads the collarbone and upper arm they write). Each driver measures the pose's local values and the model matrices, writes its bones into the pose buffer, and runs `RunLocalToModel` from the bone it changed. Like every additive modifier it owns no bones (`DrivesBone` is false), so the twist bones and rib helpers stay editable, and keys on them add to what the drivers do.

`DescribeModifier`: the write roots are each driver's twist 1 or rib helper; the reads are the upper arm (arm twist), the chest and upper arm (armpit), and the chest and collarbone (rib helper).

**UI**
- **Create:** **Add Corrective Drivers** on the Create menu or an actor's context menu, with that actor active.
- **Properties panel:** Enabled, Weight, and a weight per driver ("Left Arm Twist", "Right Armpit"...).
- **Outliner:** a leaf under its actor. **Timeline:** driver lanes are labelled "<driver> Weight".

### 2.11 Baking

Baking samples the active sequence at a fixed rate through the normal evaluation, so an export is what playback shows: sampling, hands, FK, the eval plan. The result is one actor's final local pose (after every modifier) at each sample, as a `RawOzzAnimation`. That's the format-neutral intermediate the GLTF importer also produces, so ProjectEx knows nothing about file formats.

```cpp
static size_t GetBakeSampleCount(frameCount, frameRate, sampleRate);
expected<BakeJob, string> BeginBake(actor, sampleRate) const;
expected<bool, string>    ContinueBake(BakeJob&, budget);   // true once done
size_t                    CountUnkeyedEdits() const;
```

**No dropped samples.** Samples are never captured from real-time playback. Sample *i* is evaluated at exactly `t = i / sampleRate`, whatever the game's frame rate:
- `duration = frameCount / frameRate`.
- There are `ceil(duration · rate − 1e-4)` samples at `i / rate`, plus a last one at exactly `duration`, even when the rate doesn't divide the length. The epsilon keeps float error from adding a sample just before the end.
- Each sample uses the fractional frame `t · frameRate`, and the last one uses `frameCount` exactly.
- The last sample is kept for cyclic sequences too. It's the same pose as frame 0, but where the end falls depends on the sample rate.
- At most 100,000 samples.

**A slice** (`ContinueBake`) runs until the job is done or the budget has passed:
1. **Refuse** if anything changed since `BeginBake`. `m_revision` counts every applied change, undo and redo included; the active sequence must be the same, no edit may be open, and the actor must still be bound to the same skeleton. Playback doesn't count as a change: it bypasses `Apply`.
2. **`Evaluate()`**, to bring the roots up to date. The actor must have a root (be loaded), since world-space targets are measured against it. Nothing in the slice moves a placement, so the roots stay put.
3. **`CaptureAll`** the live values.
4. **For each sample:** set `m_bakeFrame`, which `GetSampleFrame()` returns while it's set; then `ResampleAll()`, `Solve(false)`, and append the actor's back pose buffer.
5. **Restore** the live values and set `m_solveNeeded`.

Because of steps 3 and 5, the project is the same before and after every slice: `m_frame` and the playback time are never touched, and nothing goes into history. `Solve(false)` doesn't swap the pose buffers, so the game never sees the poses being baked. The next `Evaluate` republishes the real pose. Every actor is solved, not only the baked one, because a modifier can read another actor's bones through locators. Samples don't depend on each other: `PackPose` restarts from the live values, and cycle groups restart from FK.

**Keys.** Each joint gets a translation, rotation and scale key per sample. Each rotation is normalized and kept in the previous key's hemisphere, so interpolating between samples takes the short way.

**What gets baked.** Tracked properties are sampled, so unkeyed edits on them are lost, the same as pressing Play. Unkeyed edits on untracked properties stay, as constants. `CountUnkeyedEdits` counts the first kind, so the export dialog can warn about them.

**GLB output** (`GLTFExport`):
- One node per joint, carrying the skeleton's local rest pose.
- Linear translation, rotation and scale channels per joint. The time accessor is shared through buffer deduplication.
- `min`/`max` only on the time accessors, where glTF requires them.
- **The actor's mask overrides** (§2.12) go in the animation's extras, by bone name: `"extras": { "unmasked_bones": [...], "masked_bones": [...] }`. `BeginBake` copies them into `RawOzzAnimation::unmaskedBones` and `maskedBones`. Each key is left out when its list is empty, and the extras are left out when both are.
- The level picks keyframe reduction. 0 keeps every key; 1 to 4 run the ozz optimizer at tolerances 0, 1e-7, 1e-6 and 1e-5. If the optimizer fails, every key is kept.
- **Empty tracks are written as one identity key.** To ozz an empty track is identity, and the optimizer empties tracks that are constant identity (at tolerance 0, a rotation within float precision of it counts). glTF reads an empty or missing channel as the node's TRS, which is the rest pose. Leaving them out made straight finger joints come back bent, at any level above Lossless.
- `ExportToFile` gzip-compresses the file (`zstr::ostream` over a `std::ofstream`, since `zstr::ofstream` only takes a narrow path; `GLTFImport` reads compressed and plain files alike). It writes `<file>.tmp` and then renames it over the target. It doesn't touch game state, so it runs on a worker thread. `Settings::GetFaceMorphs()` initializes on first use, so the caller initializes it on the main thread first.

### 2.12 Bone mask

A graph only writes the bones its mask allows to the game. A skeleton's `defaultBoneMask` leaves out bones that aren't in the game's Havok skeleton (NIF-only bones, such as physics bones), so by default the Studio can pose them but the actor never shows it. Each actor can override the mask per bone.

```cpp
bool IsBoneMasked(ElementRef bone) const;                       // effective: the override, else the default
bool SetBoneMasked(span<const ElementRef> bones, bool masked);  // one commit for every actor touched
bool ResetBoneMask(ObjectId actor);
```

- **Storage.** `maskOverrides` holds only bones that differ from the default. `SetBoneMasked` and the importer drop overrides that match it.
- **Reaching the game.** The mask goes through the generator rather than the graph's own `boneMask`. The graph's mask is rebuilt from the default whenever the actor's 3D loads, and it would outlive the project.
  1. `ActorObject::PublishBoneMask` computes the effective mask (`true` = written) and stores it in `SharedPose` (`SetMask`). It runs from `AttachStudioGenerator` (`GameBridge`) (so on add, bind, load and undoing a removal) and from `BoneMaskChange`.
  2. The StudioGenerator copies it in `Generate`, on the animation thread, only when `SharedPose`'s mask version has changed.
  3. `Generator::GetBoneMask()` returns it. `Graph::PushAnimationOutput` uses a generator's mask in place of `loadedData->boneMask` while that generator is attached, provided it has one entry per joint.
- **Releasing bones.** Nothing in the game writes NIF-only bones, so they keep the last value written. When the applied mask changes, `PushAnimationOutput` writes the rest pose to any bone that was unmasked, is now masked, and isn't controlled by the game (`controlledByGameMask`). On close, the 0.2 s detach transition blends those bones to their rest pose too, because the game-side pose of a bone the game doesn't control is its rest.
- **Exported clips keep their mask.** The GLB records the overrides (§2.11), and NAF plays the clip back with the same mask:
  1. `GLTFImport` reads the animation's `unmasked_bones` and `masked_bones` extras into `RawOzzAnimation`.
  2. `CreateRuntimeAnimation` resolves those names case-insensitively against the target skeleton into `OzzAnimation::boneMask`, starting from `defaultBoneMask`. If a bone appears in both lists, `masked_bones` wins. Unknown names are ignored. The mask stays empty, meaning the default, unless it differs from the default.
  3. `IBasicAnimation::GetBoneMask()` returns it (null by default), and `LinearClipGenerator::GetBoneMask()` passes it on to the graph the same way the StudioGenerator does.

  Switching from a clip that unmasks a bone to one that doesn't snaps that bone to rest at the start of the transition, rather than blending it out.

### 2.13 Rigs

A rig is one actor's setup, saved apart from any project, so it doesn't have to be rebuilt for every new project. It holds:
- the actor's **pose** (its bones' live values),
- its **modifiers** (IK chains, hands, spline IK) and **the locators they own** (effectors, poles, spline controls). Other locators aren't part of a rig.
- its **bone mask** overrides,
- and, in the rig's `"editor"` section, its **bone display** (StudioMenuEx state, §3.6).

It can be applied to any actor with a compatible skeleton id. Bones are matched by name, so a missing bone is a warning, not an error.

**Saving** (`StudioProjectExport::SaveRig`):
- The rig stores **current values, not defaults.** Each modifier's and locator's live values are written as its defaults, so the new objects start, and reset to, what was on screen.
- The pose holds only bones whose live values differ from the rest pose.
- **Locators:** `ProjectEx::GetRigLocator` decides how each locator is stored.
  - Parented to the actor (root or bone), or to another locator in the rig: the parent and the local transform are kept.
  - In scene space: stored **relative to the actor's placement** as `inverse(ToMatrix(placement)) * local`. Placement has no scale, so a differently scaled target gets the same offsets in game units.
  - Parented to anything else (another actor, or a locator outside the rig): converted to scene space where it stands, then stored as above.

**Applying** (`StudioProjectImport::ApplyRig` → `ProjectEx::ApplyRig`):
1. **The importer checks the actor.** It must be bound, and its skeleton id must be compatible with the rig's (`Settings::AreSkeletonsCompatible`), or nothing changes. A gender mismatch is only a warning.
2. **It resolves names.** Bones are resolved against the actor's skeleton, and objects are built with the importer's `BuildObject`, the same code that opens projects. A modifier whose bones don't fit is dropped with a warning, as when opening a project (§4.4). A missing locator parent bone falls back to the actor root.
3. **`ProjectEx::ApplyRig` makes one commit, "Apply rig":**
   - **Selection.** Keeps the modifiers of the types in `RigOptions` (`ikChains`, `hands`, `splineIKs`, `correctiveDrivers`), and only the locators those modifiers use. Corrective drivers are skipped, with a warning, when the actor still has its own.
   - **Left-out locators.** A kept locator parented to a left-out one takes that locator's parent, with both transforms combined.
   - **New ids.** Every kept object gets a new id, and the rig's actor id maps to the target actor.
   - **Placement.** Scene-space locators are placed at `ToMatrix(placement) * stored`, as defaults and live values.
   - **`replaceModifiers`.** Removes the actor's existing modifiers of the applied types first, through `RemoveObject`, which takes their locators too.
   - **`pose`.** Sets every bone to the rig's value, or to rest for bones the rig doesn't list, as `LiveValueChange`s. Unchanged values are skipped. The changed refs are returned in `RigResult::poseChanges`.
   - **`boneMask`.** Replaces the actor's overrides, dropping any that match the default.
   - **Insertion.** Inserts locators and modifiers in the rig's order: each modifier after its locators, modifiers in their saved application order.
   - **Self-dependency.** Only possible when a locator took a left-out locator's parent. Such locators are moved to scene space with `RecordReparent` (keeping their world transform) until `HasSelfDependentModifier` is false, each with a warning.
4. **The Apply Rig dialog (`RigDialogs`) finishes in the same commit.** It wraps the call in `BeginEdit("Apply rig")` and, when **Key Pose** is on, calls `KeyCurrentValue(poseChanges)`. Keying is a UI preference (§1). It then applies the bone display, which isn't in history.

Pose edits are unkeyed unless keyed, so on tracked bones the next frame change overwrites them, like any unkeyed edit (§1). Key Pose defaults to Auto Key.

---

## 3. StudioMenuEx

A singleton. `ImguiMenu` calls `NewProject(name)` or `OpenProject(path)`. While `HasOpenProject()` is true, `ImguiMenu::Draw` hands the frame to `StudioMenuEx::Draw`.

**Structure.** `StudioMenuEx` runs the lifecycle below and owns the camera and the dock layout. An open project is a `StudioSession` (`UI/Studio/`), which `StudioMenuEx` creates when editing can start and destroys on close. The session holds the project and what its panels share: selection and selected keys, the interaction, editing preferences, bone display, the key clipboard and the status line. It also owns the panels and dialogs, each of which keeps its own view state. Panels reach shared state and operations through the session (`m_session.Select(...)`, `m_session.selection`), and each other through its members (`m_session.timeline`).

### 3.1 Session states

```
(none) ──NewProject──────────────────────────────▶ Editing
   │                                                  ▲
   └─OpenProject─▶ Binding (m_pendingOpen) ──Open──▶ Placing (m_placingScene) ──Place──┘
                         │ Cancel → close              │ Cancel → close, restore actors
```

**Binding**
- `StudioProjectImport::Read` succeeded. `DrawBindingDialog` shows one combo per slot.
- Candidates are loaded actors that pass `CheckActorCompatibility`: the last-bound actor, the console selection, and nearby actors (up to 32 high-process actors plus the player, sorted by distance).
- An actor already chosen for another slot is disabled.
- A slot is pre-filled with its hint if that actor is loaded and still fits.
- Open is enabled when every slot is filled. It calls `FinishOpen` → `StudioProjectImport::Build`. A failure shows its error in the dialog.
- A project with no actors skips this step.

**Placing**
- The project is built, so the actors are already held at their placements around the center.
- The center starts at slot 1's world placement, or `kDefaultSceneCenterDistance` (100) units in front of the camera when there are no actors.
- A `TRANSLATE | ROTATE_Z` gizmo calls `SetSceneCenter`, with markers and lines to each actor's target placement. The free camera works.
- **Place** starts editing and shows the load warnings popup if there are any.
- **Cancel** closes the project and holds each actor at its original placement.

**Editing**
- The full UI.
- `NewProject` sets the center to the player and starts here directly.

`StartSession` creates the session (which takes the project, name and path), enables docking and turns on the studio camera. The Binding and Placing steps are in `UI/Studio/OpenFlow.cpp`.

### 3.2 Frame (`Draw`)

1. `ImGuizmo::BeginFrame()`.
2. If placing, run the placement step and skip to the last step.
3. `session.ValidateSelection()`: drop selected elements and keys that no longer exist, since undo can remove them.
4. `camera.Update()`, then `session.HandleKeybinds()`.
5. Dockspace; `BuildDefaultLayout` on first use. Layout: options bar on top, outliner on the right with Properties and History tabbed below it, timeline at the bottom.
6. `session.Draw()`: the options bar, outliner, properties, timeline, history, viewport (with the gizmo) and popups, then end a field edit whose widget is no longer ImGui's active item (see **Field edit** in §3.4).
7. A deferred close (`session.closeRequested`).
8. `project.Update(dt)`, last, so everything above draws the pose the game is showing this frame (§2.7).

### 3.3 Actions and keybinds

`enum class Action`. Every menu item, context entry and keybind goes through `CanExecute` and then `ExecuteAction`. `ActionMenuItem` shows the label from `GetActionLabel` and the shortcut text.

| Action | Default chord |
|---|---|
| Save | Ctrl+S |
| Export GLB | — (at least one bound actor) |
| Close | — |
| Undo / Redo | Ctrl+Z / Ctrl+Y |
| Play/Pause | Space |
| Next / Previous Frame | → / ← (repeat) |
| Next / Previous Key | ↑ / ↓ (repeat) |
| First / Last Frame | Home / End (respects the loop range) |
| Insert Key | I |
| Delete | Delete (keys if the timeline is focused, otherwise non-bone objects) |
| Copy / Paste Keys | Ctrl+C / Ctrl+V |
| Select All | Ctrl+A (keys in visible lanes, or all bones of the active actor) |
| Rename | F2 |
| Translate / Rotate / Scale | T / R / Y |
| Toggle Local/World | X |
| Create IK Chain | Shift+I (exactly 3 bones of one actor) |
| Create Finger Controls | — (exactly 1 bone: the hand) |
| Create Spline IK | — (2 bones of one actor, one an ancestor of the other with a bone between; opens a dialog for the control count) |
| Add Actor | Shift+A |
| Add Locator | Shift+L |
| Focus Selection | F |

Keybinds are ignored:
- while typing,
- during mouse-look,
- while any popup is open.

During an interaction, only Escape (cancel) is handled.

### 3.4 Interactions

`Interaction` is a variant: `monostate`, `Gizmo`, `KeyDrag`, `Scrub`, `BoxSelect` or `FieldEdit`. At most one runs at a time, and each owns a project edit (`BeginEdit`) for its duration. `CancelInteraction` calls `CancelEdit`.

- **Gizmo**
  - Targets are sorted parents-first (`GetHierarchyDepth`: actors -1, then bones by depth, then locators) and pivot on the active element. Actors must come before their bones: a root bone edited before its actor's placement would be moved twice.
  - Each frame, every target's world is computed as `delta * startWorld`, and all of them are applied in one `SetWorldTransforms` call (§2.8). For rotate and scale it's about the target's own origin, so bones don't drift from their parents.
  - It relies on LiveValueChange coalescing, not CancelEdit.
  - Auto-key is applied per frame.
  - Targets: bones not driven by IK, locators, and actors (placement only: translate and rotate, not scale).
- **Key drag:** each frame it calls `CancelEdit()`, then `MoveKeys(original, delta)`. This keeps keys it passes over intact. A click without movement produces an empty commit, which is dropped.
- **Scrub:** calls `SetFrame` on the ruler while the mouse is held. Frame changes coalesce into one commit.
- **Field edit:** a Properties drag, slider or text field. Right after the widget, `TrackFieldEdit(description, changed, apply)` opens the interaction when the item activates, runs `apply` if the value changed, then closes the interaction when the item deactivates. That order puts a slider's jump to the click (set on its activation frame) and a typed value (committed on its deactivation frame) in the same undo step as the rest of the edit. `apply` usually calls `ApplyFieldValue`, which sets the value and auto-keys it, and mustn't submit ImGui items.
  - A field that stops being drawn while it's active never reports its deactivation, for example after typing into it and then selecting an element of another type in the Outliner, which draws before the Properties. `Draw` ends a field edit whose id isn't `ImGui::GetActiveID()` at the end of the frame, so the UI isn't left locked.

### 3.5 Windows

**Options bar** (no tab bar)
- Menus:
  - **File:** Save, Export ▸ GLB…, Save Rig…, Apply Rig…, Close. The rig items act on the active element's actor (a modifier's owner), otherwise on the first bound actor (`GetDefaultActor`).
  - **Edit:** Undo/Redo with descriptions, key operations, Auto Key, Default Interpolation.
  - **Create:** Add Actor ▸, Add Locator, Create IK Chain, Create Spline IK, Create Finger Controls, New Sequence.
  - **Playback.**
  - **View:** window toggles, Bones / Locators / IK Lines / Scene Center, Studio Camera, Camera Speed, Focus.
- A sequence combo. Its context menu has New, Duplicate, Rename, Settings, Cyclic and Remove.
- Frame and fps readout, project name with `*` when unsaved, and a status line that fades after 6s.
- Right-clicking empty space opens the Create items.

**Outliner**
- A bone filter.
- Actors, each with:
  - a **Skeleton** node, as a hierarchy or as a flat list while filtering. Hidden bones are dimmed. Masked bones are tagged "(masked)".
  - its modifiers: IK chains listing their effector and pole, spline IKs listing their controls, and hands as leaves.
  - locators parented to it.
- Then scene-space locators. Locators owned by a chain or spline are listed only under it.
- The session's `reveal` expands and scrolls to an element, for example after a 3D pick or creating an object. It uses `SetNextItemOpen(true)`, not `DefaultOpen`, because DefaultOpen only applies to nodes without stored state.
- Context menus by element:
  - **Bone:** Remove (non-bone selection), Create IK Chain, Create Spline IK..., Create Finger Controls, Add Locator Here, Insert Key, Reset Transform, Display ▸, Mask ▸ (Mask / Unmask, and the same for the hierarchy), Bezier Mode ▸ (Local / Continuous, and the same for the hierarchy), Select Hierarchy.
  - **Actor:** Rename, Bind ▸ (Previous: …, Console Selection, Nearby ▸, By Form ID…), Unbind, Add Locator, Reset Pose, Reset Bone Display, Reset Bone Mask, Bezier Mode ▸, Save Rig…, Apply Rig… (bound actors only).
  - **IK chain:** Rename, Enabled, Shoulder ▸ (None / any ancestor of the upper bone), Bezier Mode ▸, Select Effector/Pole.
  - **Hand:** Rename, Enabled, Bezier Mode ▸, Presets ▸, Copy Pose To ▸.
  - **Spline IK:** Rename, Enabled, Bezier Mode ▸, Select Controls.
  - **Locator:** Rename, Reparent ▸ (Scene / actor ▸ Root / bones / locators), Insert Key, Reset Transform, Set Current as Default (`SetLocatorDefaultsToCurrent` on every selected locator, one commit; also repairs projects saved with bad defaults), Bezier Mode ▸.
  - **Bezier Mode ▸** acts on every selected element's properties: an actor brings all its bones and its modifiers, and a modifier (or an actor's modifiers) brings its effector and pole or its controls. It checks a mode when every property has it, and skips bool properties.

**Properties**
- Gizmo mode and space.
- Keying: Auto Key and interpolation.
- The active element:
  - **Bone:** T/R/S with key indicators, disabled when driven by IK. A **Display** section with Visible and Display Size, and a **Game Output** section with Masked. Both sections apply to all selected bones.
  - **Actor:** binding info, plus a **Placement** section (Position, Heading in degrees).
  - **Locator:** its parent label and T/R/S.
  - **IK chain:** chain properties (including Twist Distribution), and a **Shoulder** section (bone, weight, max angle, Set Neutral). The viewport draws the shoulder guide (§2.10) while the chain or its effector is selected.
  - **Hand** and **Spline IK:** see §2.10.
  - Non-keyable settings (shoulder limits, finger setup) go through `TrackFieldEdit` too, so one drag is one undo step.
- **Key indicator:**
  - A diamond: hollow means animated, filled yellow means keyed at this frame, orange means edited but not keyed.
  - Click to set the key, or remove it.
  - Right-click → Cyclic toggle, Bezier Mode ▸. The tooltip notes a Continuous track.
  - A small ring shows Cyclic: green when applied, orange when on but no key is inside the sequence.

**Timeline** (custom drawn)
- **Ruler:** scrub area, the loop range band, and the playhead.
- **Lanes, built each frame from the selection:**
  - One group lane per element, summarizing all its properties. A whole actor stays a single lane.
  - Then one lane per property, unless the group is collapsed.
  - The region after `frameCount` is shaded.
- **Mouse:**
  - Click, Ctrl-click and Shift-click on keys select (replace, toggle, add). Dragging moves them; dragging empty space box-selects.
  - Wheel scrolls lanes, Shift+wheel or middle-drag pans, Ctrl+wheel zooms around the mouse.
- **Right-click:**
  - **Key area:** Cyclic ▸, Bezier Mode ▸, Interpolation ▸, Copy/Paste/Delete, Set Loop Start/End at Playhead, Clear Loop Range.
  - **Lane label:** Cyclic ▸ On/Off and Bezier Mode ▸ for that lane's properties.
- The loop range (`Timeline::GetLoopRange`) is UI state, passed to `Play()`.

**History:** a list of commits. Click one to `JumpToHistory`; entries past the head are dimmed.

**Viewport overlay** (`Viewport::Draw`)
- **Bones** are drawn as diamonds from parent to child. Root bones get an 8-unit stub pointing up.
  - Width is `clamp(length * 0.1, 0.25, 1.5) * BoneDisplay.scale`.
  - Hidden bones are skipped: neither drawn nor pickable.
- **Locators** are 4-unit diamonds:
  - effectors are green when reached, red when not;
  - poles are blue;
  - spline controls are orange;
  - other locators are yellow.
- **IK lines** run from the end bone to the effector and from the middle bone to the pole. Spline curves are drawn with them.
- Bones driven by a modifier (`IsDrivenByModifier`) are drawn green.
- The **scene center** is a pink ground cross with a longer forward arm (`kSceneCenterSize` 30).
- Hover picks the nearest diamond under the mouse. A click selects, honoring the select mode, and reveals the element in the outliner.

**Popups**
- A request is deferred (`RequestPopup` → `Popups`, opened at the root next frame), so they can be requested from inside menus.
- The popups: Add Actor menu, Add/Bind by Form ID, Rename, New Sequence, Sequence Settings, Confirm Close (Save / Don't Save / Cancel), Load Warnings, Create Spline IK (control count), Export GLB, Save Rig, Apply Rig, Rig Warnings (Load Warnings' list, with its own heading).
- Applying Sequence Settings changes the length first and then the frame rate, so rescaling uses the new length.

**Export GLB.** Always exports the active sequence. The dialog has three states:
1. **Settings.**
   - **Actor:** unbound actors are listed but disabled. It defaults to the active element's actor (for a modifier, its owner), otherwise the first bound actor.
   - **Sample Rate:** 1–240 fps, defaulting to the last rate used, otherwise the sequence's rate. The sample count is shown below it.
   - **Compression:** Lossless (level 0) or Level 1–4 (§2.11).
   - **File:** defaults to `<last export folder, or <Data path>/Exports>/<project>_<sequence>_<actor>.glb`, sanitized, shown relative to the game folder when inside it, and follows the chosen actor until it's edited. Relative paths are under the game folder (so `Data\NAF\Exports\...` works as typed), and `.glb` is added when missing. The resolved path is shown when it differs, and a warning when the file exists.
   - Warnings for unkeyed edits (`CountUnkeyedEdits`) and for the last error.
2. **Baking.** A progress bar. Each frame `ExportDialog::Draw` runs one `ContinueBake` slice (`kBakeBudget`, 8 ms), before `Update`. The modal blocks edits; a change anyway fails the bake (§2.11). Cancel or Escape drops the job.
3. **Writing.** `ExportToFile` runs on a `std::async` worker. It owns the baked animation and a `shared_ptr` to the skeleton, so it uses nothing of the project. It can't be cancelled. On success the dialog closes, the status line names the file, and the sample rate and folder are remembered. Closing the project waits for a write still running: the dialog is destroyed with the session.

**Rigs** (§2.13). Rigs live in `<Data path>/Rigs/<name>.nafrig`.
- **Save Rig:**
  - The dialog has an actor combo (bound actors only), the actor's modifier counts, and a name, which defaults to the actor's name.
  - It shows the resolved path, and a warning when the file exists.
  - It always saves everything; what gets applied is chosen when applying.
- **Apply Rig:**
  - **Rig list.** The rigs folder is read when the dialog opens, or on Refresh. Rigs for another skeleton, and files that can't be read, are listed disabled, with a tooltip giving the reason. A summary of the selected rig is shown below the list.
  - **Options.** Pose (with Key Pose under it), IK Chains, Finger Controls, Spline IK, Replace Existing, Bone Mask and Bone Display.
  - **Result.** Warnings open the Rig Warnings popup, and failures are shown in the dialog.
- **Bone display.** The rig's `"editor"` section holds the actor's bone display by bone name (`RigDialogs::WriteEditorJson`). Applying it replaces that actor's display (`RigDialogs::ReadEditorJson`).

**Camera:** the studio camera toggles free-camera mode and restores the previous camera state only if the studio changed it.
- Hold the right mouse button outside the windows to mouse-look (cursor locked).
- WASD moves, Q/E go down/up, Shift is 4× speed, and the wheel changes speed (0.05–20×).
- Focus moves the camera to 150 units behind the target along the view direction.

### 3.6 View state (not in ProjectEx, not in history)

- In the session: `boneDisplay`, a `map<ElementRef, BoneDisplay{visible, scale}>` that stores only bones differing from the default; `windows`; gizmo mode and space; auto-key; default interpolation.
- In the panels: the viewport's `view` options, the timeline's zoom, scroll, loop range and collapsed lanes, and the outliner's filter.
- All of it lives in the session, so closing the project clears it and the next project starts from the defaults. Only the camera speed and the dock layout carry over. Most of it round-trips through the `"editor"` section (§4.5).

---

## 4. File format (`.nafproject`, version 8)

Files live in `<Data path>/Projects/<name>.nafproject` (`Util::String::GetDataPath()`).

### 4.1 Container

```
"NAFSTUDO" (8 bytes) | u32 version | u64 jsonSize | JSON (jsonSize bytes) | binary key blob
```
- Little-endian. Constants are in `StudioProjectFormat.h`.
- The writer uses nlohmann (`ordered_json`); the reader uses simdjson (DOM) and caps the JSON at 64 MB.
- **Writing is atomic:**
  1. Write `<file>.tmp`.
  2. Copy any existing file to `<file>.bak`.
  3. Rename the temp file over the target.
- **Version policy:** newer versions are refused, and so are versions older than `kOldestReadableVersion`. Version 1 was the old Project/Clip format and version 2 predates scene-relative placement. Bump `kVersion` for any breaking change. Optional new JSON fields don't need a bump if the reader treats them as optional.
  - **Version 4** added object types (hands, spline IK), so older builds must refuse it. It's otherwise a superset of version 3, so version 3 files still read (`kOldestReadableVersion` = 3). Fields added in version 4 are optional on read, with the defaults a new object would have.
  - **Version 5** added each finger's `straighten` and `maxExtend`, which change what curl values mean, so older builds must refuse it rather than ignore them. Both are optional on read: a version 4 hand without them keeps curl 0 as its rest pose.
  - **Version 7** added each sequence's `bezierModes`, which change the curves' shape, so older builds must refuse it. It's optional on read: without it every track is Local.

### 4.2 Binary key records

```
i32 frame | u8 interpolation | u8 tangentMode | value
value: float 4 | bool 1 | Float3 12 | Quaternion 16 (xyzw)
```
- Auto tangents aren't stored; they're regenerated on load, so tangent fixes apply to old files.
- A future manual tangent mode would append its tangents after the value, and would need a version bump or per-key size handling.

### 4.3 JSON schema

```jsonc
{
  "version": 5,
  "nextObjectId": 12, "nextSequenceId": 3,
  "activeSequence": 1, "frame": 0,
  "objects": [
    { "id": 1, "type": "actor", "name": "Piper",
      "skeleton": "<skeleton id>", "gender": "male|female|any",
      "modifiers": [4],
      "placement": { "translation": [x,y,z], "heading": 0.0 },
      "bindingHint": { "formId": 0x0002F1F, "plugin": "Fallout4.esm", "localFormId": 0x2F1F, "name": "Piper" },
      "boneMask": [ { "bone": "Breast_CBP_L_02", "masked": false } ] },  // optional; overrides of the default mask
    { "id": 2, "type": "locator", "name": "...",
      "parent": { "object": 1, "bone": "RArm_Hand" },        // object 0 = scene; bone omitted = actor root / locator
      "defaults": { "translation": [..3], "rotation": [..4], "scale": [..3] } },
    { "id": 4, "type": "ikChain", "name": "...", "actor": 1,
      "bones": ["upper", "middle", "end"], "middleAxis": [..3],
      "effector": 2, "pole": 3,
      "defaults": { "weight": 1, "soften": 0.95, "twist": 0, "enabled": true, "shoulderWeight": 0.5,  // shoulderWeight: v4, optional
                   "twistDistribution": 1 },  // v6, optional
      "shoulder": { "bone": "RArm_Collarbone", "maxAngle": 0.52,  // v4, optional; bone omitted = none
                   "neutral": [..3] } },  // v6, optional (shoulder parent space; omitted = end bone at rest). Before v6, shoulderWeight is ignored (default)
    { "id": 5, "type": "hand", "name": "...", "actor": 1, "handBone": "RArm_Hand",
      "fingers": [ { "bones": ["...", "..."], "curlAxes": [[..3], [..3]], "spreadAxis": [..3], "spreadScale": 1,
                     "maxCurl": [1.57, 1.75], "maxSpread": 0.26, "flip": false,
                     "straighten": [-0.21, -0.68], "maxExtend": [0.35, 0.09] } ],  // v5, optional together; omitted = curl 0 is rest
      "defaults": { "curl": 0, "spread": 0, "weight": 1, "enabled": true, "fingers": [ { "curl": 0, "spread": 0 } ] } },
    { "id": 9, "type": "splineIK", "name": "...", "actor": 1,
      "bones": ["SPINE1", "SPINE2", "Chest"],                 // the whole path, start to end
      "aimAxes": [[..3], ...], "upAxes": [[..3], ...],        // one per bone
      "controls": [6, 7, 8], "controlAxes": [ { "aim": [..3], "up": [..3] }, ... ],
      "rootFollowsCurve": false, "alignTip": true,
      "defaults": { "weight": 1, "twist": 0, "stretch": 0, "tangentLength": 1, "orient": 1, "enabled": true } },  // orient: optional
    { "id": 10, "type": "correctiveDriver", "name": "...", "actor": 1,  // v8
      "drivers": [ { "kind": "armTwist|armpit|ribHelper", "mirrored": false, "bones": ["LArm_UpperArm", "LArm_UpperTwist1", "LArm_UpperTwist2"] } ],
      "defaults": { "weight": 1, "enabled": true, "driverWeights": [1] } }  // one per driver
  ],
  "sequences": [
    { "id": 1, "name": "Sequence 1", "frameRate": 30, "frameCount": 30, "loopSmoothing": false,
      "tracks": [ { "object": 1, "bone": "Pelvis", "property": "rotation",
                    "type": "quaternion", "offset": 0, "count": 4 } ],
      "loopSmoothedProperties": [ { "object": 1, "bone": "Pelvis", "property": "rotation" } ],
      "bezierModes": [ { "object": 1, "bone": "Pelvis", "property": "rotation", "mode": "continuous" } ] }  // v7, optional; only non-Local
  ],
  "liveValues": [ { "object": 1, "bone": "Head", "property": "rotation", "value": [..4], "edited": true } ],
  "editor": { /* opaque, owned by StudioMenuEx */ }
}
```

- **Bones are referenced by name** everywhere, so a skeleton whose joints were reordered still loads.
- **Hand finger properties** (in tracks, `loopSmoothedProperties`, `bezierModes` and `liveValues`) use `"finger": <index>` in place of `"bone"`, and **corrective driver weights** use `"driver": <index>`. The importer keeps either as `FileProperty::element`.
- **Corrective drivers** whose bones are missing on the bound skeleton are dropped one by one with a warning; the object is removed only when none are left.
- **Binding hint:** stored as plugin plus local form ID, so it survives load-order changes. Light plugins (0xFE) mask to 0xFFF, others to 0xFFFFFF. Runtime forms (0xFF) store only `formId`.
- **`liveValues`** holds only values that can't be recreated from defaults plus sampling:
  - edited properties, and
  - properties untracked in the active sequence whose value isn't the default, such as a property whose keys were all deleted.

### 4.4 Import pipeline

```cpp
StudioProjectImport::Read(path)  -> expected<PendingProject, string>   // no game side effects
PendingProject::GetActorSlots()  -> span<ActorSlot{id, name, skeletonId, gender, bindingHint, bindingHintName}>
PendingProject::GetEditorJson()  -> minified "editor" object or ""
StudioProjectImport::Build(pending, span<Actor*> oneActorPerSlot) -> expected<Result{unique_ptr<ProjectEx>, warnings}, string>
```

**Read**
- Parses into intermediate `File*` structs: `FileObject` with variant data, `FileSequence`, `FileTrack` with a `KeyList` variant, and `FileLiveValue`.
- Resolves binding hints: by plugin and local form ID when the hint has them, otherwise by the raw form ID. There's no fallback to the raw ID when the plugin lookup fails, since that ID could now be a different form. A hint counts only if the form is an Actor.

**Validation in Read (any failure rejects the file)**
- **Container:** magic, version and sizes are valid. Every track range fits in the blob and the count is non-zero.
- **IDs:** unique and non-zero, for both objects and sequences.
- **References:**
  - A locator's parent is scene (no bone), an actor, or a locator (no bone).
  - A chain's actor, effector and pole are the right types. A spline's actor and controls are the right types.
  - Each locator serves at most one modifier (as an effector, a pole or a control).
  - Each actor's modifiers are exactly its own chains, hands and splines; every modifier is listed by its actor exactly once.
  - A hand has at least one finger, and each finger's bones, `curlAxes` and `maxCurl` have the same length. `straighten` and `maxExtend` are either both absent or both that length too. A spline has at least 3 bones, one aim and up axis per bone, at least 2 controls, and one `controlAxes` entry per control.
- **Properties:**
  - Each property belongs to its object type (bones only on actors, fingers only on hands, and only the properties in §2.1's table).
  - A finger index is less than its hand's finger count.
  - Each track's `type` matches the property's value type.
  - No duplicate tracks within a sequence.
- **Values:**
  - Numbers are finite.
  - Frames are ≥ 0 and strictly increasing.
  - Interpolation and tangent mode are in range, and bools are 0 or 1.
- **Sequences:** frame rate > 0 and frame count ≥ 1. At least one sequence exists, and the active sequence exists.
- **Repairs:**
  - Quaternions are normalized; a zero-length quaternion is an error.
  - `nextObjectId` and `nextSequenceId` are raised above the largest used id.

**Build**
1. Checks there's one distinct, non-null actor per slot.
2. Binds every actor with `LoadActorBinding` first, so a failure leaves nothing attached. Resolves each actor's `boneMask` names; missing bones are dropped and counted (warning), and mask overrides that match the default are dropped silently.
3. Builds the objects. Modifiers that no longer fit the skeleton are dropped with a warning:
   - a chain whose bones are missing, or no longer form a chain (its effector and pole stay as plain locators);
   - a hand whose bones are missing, or whose fingers no longer hang from the hand bone (`IsValidHand`);
   - a spline whose bones are missing, or no longer form the saved path (its controls stay as plain locators).

   A chain whose shoulder bone is missing or no longer above the upper bone keeps working without a shoulder (warning).
4. Attaches locator parents one at a time, against the parents already in place:
   - a missing parent bone falls back to the actor root (warning);
   - a parent that would form a loop, or make a chain or spline depend on itself, falls back to scene space (warning).
5. Builds the sequences. Tracks whose bone is missing are dropped and counted per sequence (warning). Tracks of dropped modifiers are dropped silently.
6. Calls `FinishLoad` with the resolved live values.

### 4.5 Editor section (StudioMenuEx)

`StudioSession::WriteEditorJson` and `ReadEditorJson` use nlohmann. The session writes the editing preferences and bone display; the Viewport, Timeline and ExportDialog write and read their own keys (`WriteSettings`/`ReadSettings`) in the same object. If reading throws, that adds a warning; it's never fatal.

```jsonc
{ "autoKey": false, "defaultInterpolation": 1, "gizmoMode": 1, "gizmoSpace": 0,
  "view": { "bones": true, "locators": true, "ikLines": true, "sceneCenter": true },
  "timelineZoom": 8, "timelineScroll": 0, "loopRange": [0, 30],
  "export": { "compression": 0, "sampleRate": 60, "directory": "C:/.../Exports" },  // sampleRate and directory once something was exported
  "collapsedLanes": [ { "object": 1, "bone": "Pelvis" } ],
  "boneDisplay": [ { "object": 1, "bone": "LArm_Finger11", "visible": true, "scale": 0.3 } ] }
```

Elements are written by bone name. Unknown elements are skipped on read, and enum values are clamped.

### 4.6 Rig files (`.nafrig`, rig version 3)

**Container**
- The same container as a project (§4.1), with magic `"NAFSTRIG"`, its own version (`kRigVersion`), and no key blob.
- Writing is atomic, with a `.bak`, through the same `WriteFile`. Reading shares `ReadContainer` with projects.
- Rig versions newer than `kRigVersion` are refused.

**Shared object code.** Objects use the project's object schema (§4.3), through the same code: `WriteObject` (with `a_live`) on export, and `ReadObjectData` / `BuildObject` on import. So a project version bump that changes an object's JSON changes rigs too. Bump `kRigVersion` along with it whenever older builds should refuse the rig.

```jsonc
{
  "version": 1,
  "actor": 1,                                   // the id the objects below use for the rig's actor
  "skeleton": "<skeleton id>", "gender": "male|female|any",
  "pose": [ { "bone": "RArm_Hand", "translation": [..3], "rotation": [..4], "scale": [..3] } ],  // bones away from rest
  "boneMask": [ { "bone": "Breast_CBP_L_02", "masked": false } ],
  "objects": [
    // Locators and modifiers in the project schema, each modifier after its locators. "defaults" hold the values
    // at save time. A locator's parent is { "object": 0 } (scene space, relative to the actor's placement),
    // { "object": <actor>, "bone"?: ... }, or { "object": <rig locator> }.
  ],
  "editor": { "boneDisplay": [ { "bone": "LArm_Finger11", "visible": false, "scale": 1 } ] }  // owned by StudioMenuEx
}
```

**Validation in `ReadRig`.** Any failure rejects the file.
- **IDs:** object ids are unique, non-zero, and differ from `actor`. There are no actor objects.
- **Locator parents:**
  - Each parent is scene space (no bone), the actor (bone optional), or a rig locator (no bone).
  - Parent loops are rejected.
- **Modifiers:**
  - Every modifier names the rig's actor.
  - Its locators are rig locators, and each locator serves at most one modifier.
- **Objects:** each object passes the same checks as in a project (§4.4).

---

## 5. Rendering helpers (DX11Renderer)

- `WorldToScreen` returns false for points behind the camera.
- **Diamonds and lines are clipped in homogeneous clip space** against `w ≥ kNearClipW` (0.1):
  - A `ScreenSpaceDiamond` stores 6 `ClipVertex{x,y,w}`: start, four ring points, end.
  - Faces (`kDiamondFaces`) are clipped with Sutherland–Hodgman (`ClipTriangle`); edges with `ClipSegment`.
  - Picking (`IsScreenPointInsideDiamond`) tests the clipped polygons.
  - Dividing before clipping caused mirrored or garbage geometry for anything crossing the camera plane. Don't regress this.
- `GetGizmoCameraMatrices` supplies the view and projection for ImGuizmo.

---

## 6. Build and environment notes

- C++23, MSVC, with a force-included PCH (std library + CommonLibF4). New source files are picked up by CMake `GLOB_RECURSE`; reconfigure after adding them.
- **Windows `min`/`max` macros:** write `(std::min)(...)` and `(std::ranges::min)(...)`.
- **ADL clash:** don't define local `Cross`, `Dot` or similar helpers that collide with `ozz::math` through ADL. Use the ozz versions.
- **Libraries:**
  - ImGui is 1.89.9 (docking branch): `ImGuiKeyChord`, `io.KeyMods`, `GetKeyChordName`, and DockBuilder from `imgui_internal.h`.
  - nlohmann-json and simdjson come from vcpkg.
- **Building from Git Bash:**
  ```
  "/c/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" \
    --build Plugin/out/build/build-release-msvc-od --config Release -- -m -v:minimal
  ```
  Use dash-style msbuild arguments, because Git Bash mangles `/m`. The build copies the DLL into the Fallout 4 folder.
- ImGui asserts on zero-size `InvisibleButton` and `Dummy` items. The timeline clamps its widths and heights for that reason.

---

## 7. Known issues and loose ends

Most of the scene-center and serialization work has only been compile-tested. Verify these in game.

1. **The heading convention is unverified.** `ToMatrix` assumes the game's `angle.z = 0` faces +Y and turns positively toward +X. If actors face wrong after a reload, fix it in `ToMatrix`/`ToPlacement` only.
2. **The actor gizmo may lag a frame.** `GetWorldTransform({actor})` used to read `rootWorld` from the actor's 3D, which follows the graph root one update later. `rootWorld` now comes from the held placement (§2.7), which should fix this; verify in game.
3. **The save/load round trip is untested in game.** That covers keys, IK chains, bone-parented locators, edited values, bone display, binding hints, and the warnings path.
4. **The scene center has no undo or save, by design.** `SetSceneCenter` while editing would silently move everything in scene space. Keep it confined to the Placing step.
5. **An unbound actor slot** (after Unbind) isn't held, has `hasRoot == false`, and locators parented to it don't resolve. That's expected, but the UI gives little feedback.
6. **The old StudioMenu, Project and Clip classes** are dead code with stubbed save/load.
7. **Auto-shoulder, finger controls and spline IK are compile-tested only.** Verify them in game, along with their save/load round trip, the version 3 → 4 and 4 → 5 read paths, and the 5 → 6 read path (old chains take the default shoulder weight and the rest-pose neutral). For hands, check in game that curl 0 is flat on every finger (especially the base knuckle and the thumb), and that −1 only bends slightly back.
8. **Finger axes are fixed at creation.** Hands created before the hinge-based detection keep their old axes (they're saved in the project); remove and recreate them. The same goes for hands from before version 5, which keep curl 0 as the rest pose. Fingers straight at rest still rely on the palm-normal estimate, which assumes actors face +Y; **Flip Curl** covers a backward finger there.
9. **Modifiers in a dependency cycle re-run their shoulder swing** each iteration, on top of the previous one. It converges, but swings further than one pass would. The same goes for spline stretch.
10. **Twist distribution and the reworked shoulder swing are compile-tested only.** Verify in game: twisting an arm effector spreads down `ForeArm2`/`ForeArm3` with no candy-wrap and no wrist drift; `twistDistribution` 0 matches the old behavior; the shoulder stays still with the effector at neutral, follows it smoothly (raising the hand shrugs, pushing forward protracts), eases into the max angle without a stop, and ignores torso motion; Set Neutral and picking a shoulder in the Outliner recapture it. The clavicle's range is asymmetric (much more elevation than depression), which one influence and limit can't express: if lowering the hand drags the shoulder down too far, split the swing into elevation and protraction weights and limits in the parent's space.
11. **Auto-key during a drag grows the commit every frame.** `TryCoalesce` stops at a change of the other kind on the same property, so a gizmo or field drag with Auto Key on records an uncoalesced LiveValueChange and KeyChange per target per frame (a 5 s drag of 20 bones is ~12k changes in one commit, all replayed on undo). Fix: key once when the interaction ends, or let a matching Live+Key pair merge into the earlier pair. Merging a LiveValueChange past a KeyChange on its own is *not* safe: the replayed key would resample over the later edit.
12. **Escape during an interaction started while playing jumps the playhead back** to where playback started. The mutator's `Pause()` runs inside the interaction's transaction, so `CancelEdit` also reverts the Playback FrameChange. Fix: `Pause()` in `BeginInteraction`, before `BeginEdit`.
13. **Edit → Delete and Edit → Select All can't act on keys.** Opening the menu moves focus off the timeline, so `Timeline::IsFocused()` is false and they act on objects and bones.
14. **A disabled IK chain keeps its last `reached`.** `IKChainObject::Solve` returns before the job runs, so the effector keeps its last red or green.
15. **`AddIKChain`/`AddSplineIK` can leave a mislabelled commit.** When they fail with "must be bound" after `Pause()`, the Playback commit is saved under their description.
16. **GLB export is compile-tested only.** Verify in game: an exported clip loaded back through NAF matches Studio playback (IK, hands and spline included); every sample is present at Lossless; a sample rate that doesn't divide the length still ends on the last frame; the actor doesn't flicker while baking; and Blender imports the file with a sane rest pose.
17. **Rigs are compile-tested only.** Verify in game:
    - A rig saved from one actor and applied to another of the same skeleton reproduces the pose, chains, hands, splines, mask and bone display.
    - Effectors and poles land in the same place relative to the new actor, including when it faces another way.
    - Replace Existing removes only the checked modifier types.
    - Key Pose keys exactly the changed bones.
    - Undo removes everything but the bone display in one step.
18. **Bone mask overrides are mostly verified in game.** Unmasking NIF-only bones works and overrides OCBP physics on them, closing the project releases them, the overrides survive a project save/load, and an exported GLB played through NAF moves its unmasked bones. Still to check: masking a bone again returns it to rest.

**Performance**

19. **`SolveSpline` allocates about 10 vectors per solve.** Left as is: next to the rest of a frame it's small, and scratch buffers would mean threading them through `SplineMath`'s pure functions.

The bulk-edit and per-frame costs listed here before are fixed: the open transaction is indexed for coalescing (§2.6), key writes regenerate only the tangents that can change (§2.4), objects are found through `ObjectStore`'s id index, the viewport caches `GetWorldToClip` per frame, rejects hover tests by screen bounds and asks for each actor's driven bones once, the outliner builds its locator hierarchy once per frame, timeline lanes are cached against `ProjectEx::GetRevision`, the selection and the collapsed groups, and Properties stops collecting gizmo targets at two.

## 8. Extension points

- **Cutscene editor:** sequences are already swappable timeline sets. A cutscene would be an ordered list of sequence references plus camera tracks. Camera would be a new object type with transform properties, saved as a new `"type"`.
- **Procedural graph drivers:** derive from `ModifierObject` (§2.10), add an `ObjectType`, and give it a `Build*` function for `ProjectEx` to record. A model-space modifier implements `GetWriteRoots` (and `AppendBoneReads` for inputs that aren't its write roots' parents or its locators) and `Solve`; the eval plan handles ordering and cycles. A local-space, additive one implements `ApplyToPose` instead, which runs before LocalToModel. Bones it fully owns go in `DrivesBone`, and the locators it owns in `GetLocators` and `Remap`. Non-keyable settings go in the `ModifierSettings` variant. Outside the model, modifier types are still named in the file format (`WriteObject`, `ReadObjectData`, `BuildObject` and the importer's checks), `RigOptions`, and the UI's per-type views (the properties panel, the outliner's listing and context menus, `GetElementProperties`).
- **Furniture and props:** a new object type with a transform (like a locator) that can be bound to a reference, held through placement like actors.
- **Manual tangents:** add a `TangentMode` value, skip it in `RegenerateTangents`, and serialize its tangents after the key value (with a version bump). In Continuous mode, a manual key would be a fixed row: its velocity becomes a boundary condition for the runs on either side.
- **Runtime keybinds:** replace the table in `GetActionChord` (`UI/Studio/Actions.cpp`).
- **New saved properties:** extend `PropertyId`, `FindProperty`/`ForEachProperty`, `kPropertyNames`, and `GetValueIndex` in the importer.
