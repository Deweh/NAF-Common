# Real-time body morphs

NAF morphs actor meshes in real time: named per-vertex offsets, read from BodySlide/LooksMenu TRI files, mixed from several sources and applied every frame they change. The first user is pose-space correctives (for example, a sculpted fix for the hip crease in a side split, faded in by how far the legs are spread), but anything that wants to move vertices goes through the same path.

It works alongside LooksMenu (F4EE) and doesn't replace it: no save-game sliders and no BodyGen. LooksMenu bakes its morphs into a mesh when the mesh is built; NAF morphs on top of whatever it built.

## Pieces

Everything lives in NAF-Common's `Morph/`, so another game's port can reuse it. Only the parts built on Fallout 4's renderer and engine structs are behind `#if defined TARGET_GAME_F4`.

| File | What | Games |
|---|---|---|
| `Manager` | The single owner of shared morph data: every actor's values, the loaded drivers and TRI files, the reload generation and the correctives switch. | all |
| `Values` | One actor's morph values in layers (`ActorMorphs`). | all |
| `Drivers` | Corrective drivers: JSON loading, RBF interpolation, evaluation on a pose. | all |
| `Correctives` | Runs the drivers on every visible loaded actor and writes its corrective values. Elsewhere, it does nothing. | F4 |
| `Geometry` | `MeshDeformer`: sums morph offsets over rest positions, welds seam copies, and works out how far the surface turned at each vertex. | all |
| `Smoothing` | Corrective smoothing: its settings (`SmoothLibrary`, owned by `Manager`) and each mesh's precomputed data (`SmoothMesh`). | all |
| `GpuSmoother` | Runs corrective smoothing for one shape in compute shaders, writing straight into its vertex range. | F4 |
| `TriFile` | TRI reader and the loaded-file library. Reading through the game's archives is F4-only. | all |
| `ShapeMorpher` | One actor's copy of one mesh shape: takes over its vertex range, reads and writes the vertex format, uploads. | F4 |
| `Backend` | Hooks, binding shapes to actors, the per-frame flush. Elsewhere, `GetShapes` returns nothing. | F4 |

**Ownership.** `Manager` owns everything shared, for the life of the plugin. Everything else is derived from it and owned by whoever derives it: `Correctives` owns each loaded actor's resolved drivers (`DriverRuntime`) and the skeleton nodes they read; the `Backend` owns each actor's bound shapes (`ShapeMorpher`), which hold the `TriFile`s they read from. Derived data is rebuilt when the manager's data generation or driver generation changes. Starfield has its own body morph and corrective system, so its backend would only hand the `Manager`'s totals to it; a Skyrim port could reuse `MeshDeformer` and `TriFile` with its own vertex format and buffers.

## Values

Each actor has morph values keyed by layer, then by morph name (case-insensitive, stored lowercase), then by owner. Every value in every layer adds up.

- **Static**: set by other code and the console (`naf morph set`), keyed by the caller.
- **Animation**: reserved for morph tracks in NAF animations. Nothing writes it yet.
- **Corrective**: written by `Correctives` from the actor's drivers.

`ActorMorphs::GetVersion` changes only when a total changes by more than 1e-4, so a frontend can skip actors whose morphs didn't change, and morphs that don't move cost nothing.

## Corrective drivers

Files in `Data/NAF/Correctives/*.json`, matched to skeletons by race EditorID (as NAF names skeletons). The format is documented at the top of `Morph/Drivers.h`. Each driver measures one bone's orientation in a reference bone's space (for a thigh: `LLeg_Thigh` in `Pelvis` space). Its axis runs to a tip bone (`LLeg_Calf`). The distance between two orientations combines swing (the angle between the axes) and twist (the turn about the axis that's left), as sqrt(swing² + (twist weight × twist)²). The twist weight defaults to 1; 0 makes a driver swing-only. Without twist, a side split with the knees forward and one with the knees up look identical to a driver, but the skinning carries a sculpt for one far off the other.

Poses are given as a rotation about the character's axes (X right, Y forward, Z up), applied to the bone at rest, plus an optional extra twist about the bone's own axis. The rest pose is always an implicit pose with every morph at 0. Values in between come from Gaussian radial basis functions of that distance (`radius`, default 60°), solved for each pose's share so each pose is hit exactly. Shares below 0 are dropped and shares totalling more than 1 are scaled down to 1: unnormalized, two neighbouring poses each get about 0.55 halfway between them, and their sculpts stack into a bulge. A pose's morph values are scaled by its share and summed. Outputs are clamped to `range` (default 0 to 1). Away from every pose they fade to 0; adding a pose with the morph at 0 pins it down where the fade isn't enough.

Correctives run on every loaded actor, whether NAF animates it or not. Once per frame, at `Main::DrawWorld_PreRender` (after the frame's animation update, just before morphs are applied), `Correctives::Update` walks the player and the high process actors (`ProcessLists::highActorHandles`). For each one with 3D that's visible (the same test the backend uses, below), it evaluates the race skeleton's drivers on the bones' local transforms, read straight from the actor's skeleton nodes, and writes `Layer::kCorrective`. Joints the actor's model lacks use the skeleton's rest pose. An actor that isn't visible keeps its last values and costs nothing. An actor that leaves the high process or loses its 3D has its correctives cleared. Outputs under 1e-4 count as 0, so an actor near rest gets no morph values and no copy of its meshes. Each actor's resolved drivers and nodes are rebuilt when its 3D, its race or the drivers change.

## Corrective smoothing

Blender's Corrective Smooth modifier (Length Weight, factor 1), run on actors' skinned meshes in real time, so skinning's collapsed and crumpled creases come out as Blender shows them. Off by default; `naf morph smooth` or the menu's Settings tab turns it on. Settings aren't saved.

Corrective Smooth smooths the skinned mesh *Repeat* times, then adds the rest mesh's detail (rest minus rest smoothed the same way) back on in the frames of the smoothed surface's face corners, averaged by corner angle. Here it's split in two:

- **Precomputed, once per mesh** (`SmoothMesh::Build`, on background threads, about half a second for a body): the *Repeat* passes with edge lengths fixed at rest. That makes them linear in the bone transforms, so a vertex's smoothed position is the sum over a few bones of the bone's transform times a precomputed 4-vector (the smoothed `weight × [rest, 1]`). Bones contributing under 1e-3 are dropped; a body keeps about 13 per vertex. Each bone's field is smoothed separately, only as far as it spreads. Meshes built from the same rest positions, triangles and weights share their data (`SmoothLibrary::Get`).
- **Every update, on the GPU** (`GpuSmoother`): the smoothed mesh from the bones; the runtime passes with the current edge lengths, as Blender does; then the detail, from the *morphed* rest positions, so every other morph goes through the smoothing as detail. Fixed lengths are what make the precomputed part differ from Blender; the runtime passes take out most of that. Against Blender at 700 repeats, 700 precomputed + 100 runtime passes is within about 1 unit on the hip poses tested (the default); 700 + 0 is within about 3.

**Region.** Smoothing every joint rounds off the ones bent far from the rest pose: an A-pose body's shoulders with its arms down puff out into a rounded cap (Blender's modifier does the same), and knees bend badly too. So it applies within a region, picked by bone: a vertex is in it by the share of its smoothed position that the region's bones carry, which fades as smoothly as the smoothing spreads. That share is remapped so groups mostly the region's are wholly in it (from 0.65; out below 0.3), since the smoothing spreads some of every vertex onto bones around it (even the hip crease is only about 75% the hip region's). Outside, a vertex is plain skinning plus its morphs, with its authored normals (or turned to the morphed surface next to morphed vertices). Regions:

- **Hips and thighs** (default): `Pelvis`, `Pelvis_Rear`, `Spine1` and `Spine1_Rear` (the top of the hip crease), the buttocks, and `Thigh` with its `_01_F` and `_01_R` helpers. Not `Thigh_Low` or `Thigh_Fat`, nor the calves. On the body: the hip crease, hips, buttocks and crotch are in, mid-thigh and waist partly, knees, abs and shoulders out.
- **Whole body.**
- **Custom**: `Data/NAF/Smoothing.json`, as `{ "bones": { "Pelvis_skin": 1.0, "LLeg_Thigh_skin": 0.5 } }`. Bone names are the skin's, case-insensitive, with or without `_skin`; strengths are 0 to 1. Read when the region is picked and by `naf morph reload`.

A shape wholly outside the region keeps the CPU path.

**Auto-smoothing** (off by default) smooths out crumpled creases afterwards, within the region, the way sculpting them out by hand does: the mask is where the skin is both squashed (from under 100% of its rest area, fully at 40%) and sharper than at rest (an edge bent from 8°, fully at 25°, more than at rest), spread four rings, and then *passes* (default 60) of plain smoothing move each vertex toward its neighbours' average by half the mask. Detail the body has at rest isn't sharper than at rest, so it's left alone, and the region keeps it off the abs. On squat, it takes the sharpest 1% of edges around the hips from 61° to 34° (hand-sculpted: 30°).

Smoothing works on weld groups, so UV seams don't open. With **Pin open edges** (on by default) a mesh's open edges keep plain skinning, so the body's neck stays where the head mesh meets it. The result is written in the shape's own vertex format: each vertex's position is carried back through its own skinning, so the game's skinning lands it on the result, and its normal, tangent and bitangent are turned from where skinning would put the rest surface's normal to the result's, the same way. Where a group's triangles are tiny or face every which way (the tips of toes), their normal is mostly rounding error, so the frames there turn with the smoothed surface around them instead.

Our compute shaders write into a buffer of ours, which is copied into the shape's range of the game's vertex buffer on the GPU; nothing is read back. The game's compute state is saved and put back around it. A shape is smoothed when it's skinned, has normals and is bound by the backend (it has a TRI source, as BodySlide bodies and outfits do), on every visible loaded actor, morphed or not. Until its data is built, or if the GPU path can't be set up (the vertex buffer isn't a default-usage buffer, for one; logged), the shape is morphed on the CPU as before, and switching back rewrites its whole range from the CPU copy. A smoothed shape is updated whenever its bones move, while its actor is visible.

Cost, measured outside the game on an RTX 5070 Ti for a 21,455-vertex body: about 0.02 ms of GPU time for the precomputed part and 0.0035 ms per runtime pass (0.36 ms at 100). The precomputed data is a few MB per body mesh.

**Correctives with smoothing on.** The smoothing does what the hip correctives' starting shapes did, so correctives exported from scenes baked with Corrective Smooth (`HipCorrectives_v3_CorrectiveSmooth.blend`) apply it twice while it's on. With smoothing, a corrective should hold only the sculpt's difference from the smoothed result.

## TRI files

The format LooksMenu reads and BodySlide writes (layout in `TriFile.h`; the magic is a multi-character constant, so it reads backwards on disk). A mesh gets its TRI file from:

- **`BODYTRI` extra data**, a path under `Meshes`, which BodySlide writes on the root of meshes built with morphs. When an actor attaches a mesh (`BipedAnim::AttachSkinnedObject`), the engine moves the skinned shapes out from under that root and drops the root, so the attach hook first adds the root's `BODYTRI` to each shape.
- **Any file in `Data/NAF/Morphs/*.tri`** with a shape of the same name.

Shapes are matched by name, but a shape's name in game can differ from the one in its mesh file (the TWB-3BBB body's does). So when a shape isn't named in its `BODYTRI` file and that file has only one shape, that shape is used, and its name is the one matched against `Data/NAF/Morphs`. This is for development and for corrective sets shipped separately from BodySlide.

Files load on a background thread, so the frame never waits on them: an actor whose files are still loading is bound a few frames later. Each file is indexed when loaded, and each morph decoded the first time it's used. A body's TRI with 179 sliders costs its 10 MB of raw bytes plus the morphs actually used. There's no memory limit yet.

## Applying (Fallout 4)

Once per frame, at `Main::DrawWorld_PreRender` (main thread, renderer locked, before the world is drawn), the backend goes through every actor whose totals changed, whose equipment was attached since the last frame, or that's still waiting on a vertex range:

1. **Bind**: walk the actor's third-person 3D and pair each `BSTriShape` that has a TRI source with a `ShapeMorpher`, keeping existing ones whose shape is still attached and still drawing from our range.
2. **Take over** a shape the first time one of its morphs is non-zero. This is the sequence `naf morphtest` proved:
   1. `CreateTriShape` copies its vertices into a new pool range, sharing the index buffer.
   2. The shape's `rendererData` is swapped to the new range.
   3. `ClearRenderPasses` runs, because the old passes hold their own reference to the shared range.
   4. The shared TriShape gets a `DecRef`.
   5. The new range is created asynchronously, so morphs wait until it's on the GPU.

   The CPU copy stays unmorphed and is the base for every update. The shape's topology (welding and the triangle tables, a few milliseconds for a body) is then built on a background thread before the first morph is applied.
3. **Apply**:
   - Sum the offsets of the active morphs and write positions for the vertices they move.
   - Recompute each nearby weld group's surface normal and rotate the mesh's own normal, tangent and bitangent by how much that normal turned. Weld groups are vertices at the same rest position, split along UV seams. Rotating the mesh's own frames, instead of replacing them, keeps its authored shading and leaves no seam where the morph ends.
   - **Posed shading.** Offsets are stored on the unposed mesh, but correctives are sculpted on the posed one, where skinning squashes or folds the crease. Unfolding it there can fold the unposed mesh over itself, so a turn measured unposed points normals inward and renders black. So for a skinned shape, nearby weld groups whose surroundings are skinned by more than one bone are shaded on the posed mesh instead: each vertex's bone weights are read once, the skin's bone transforms every frame, and the frames turn from where plain skinning would put them toward the posed, morphed surface, by as much as the morphs there are on (at full strength, exactly the sculpt's own surface in Blender), carried back through the vertex's skinning. These vertices then depend on the pose, so while any are active the actor is visited every frame it's visible (its root fade node was drawn this frame or the last: the culling pass stamps the frame number on it, and the game's animation scheduler uses the same test), and they're re-shaded and uploaded whenever their bones move relative to each other. A culled actor costs nothing until it's visible again. Groups skinned by one bone keep the unposed turn, which is exact there.
   - Restore vertices that moved last time but not now.
   - Upload only the changed vertices: runs closer than 32 vertices are merged, and each run is one `UpdateSubresource`.

A taken-over shape keeps its own range until the mesh is unloaded or replaced. If LooksMenu rebuilds the actor, the new shapes are bound and taken over again, with LooksMenu's new base.

## Authoring correctives

For the hips there's a multi-pose scene, `HipCorrectives.blend`. Each pose is a timeline frame with its own shape key, which switches itself on at that frame. The poses are side 45° and 80°, side 80° with the knees up, front 90°, back 40° and a straddle. `export_correctives.py` writes every sculpted key as `_L` and `_R` morphs into `Data/NAF/Morphs/<scene>.tri`. It also writes `Data/NAF/Correctives/<scene>.json`, with a driver per leg whose poses carry each pose's leg rotation, read from the scene's `NAF_POSES` text block. Unsculpted keys are skipped. The scene is built offline from the installed body (`hipset_prep.py`, `hipset_build.py`), and each pose's skinning matches the game's within 0.04 units.

The first, single-pose pipeline (side splits on TWB-3BBB), with tools in `Documents/NAF/Correctives`:

1. **A Blender scene** of the body in the pose, built from the installed nif. The armature reproduces the game's skinning to within 0.04 units. Seams are welded, with each vertex's original index kept in a `naf_index` attribute. The sculpt goes on a shape key, and Blender maps strokes made on the posed mesh back to the unposed mesh, which is the space the TRI offsets live in.
2. **`export_tri.py`** writes `NAF_` shape keys as a packed TRI into `Data/NAF/Morphs`. It splits each key into `_L` and `_R` halves by the side of the body, blended across the middle, writes every seam copy of each vertex (from `<shape>.weld.json`), and has been checked to match the key to 16-bit precision.
3. **A driver file** in `Data/NAF/Correctives` maps the pose to the morphs. `naf morph reload` re-reads both without restarting.

## Console

`naf morph <action> [actor form ID]`. The actor defaults to the console selection, then the player.

| Action | |
|---|---|
| `set <morph> <value>` | Static value (owner `console`). Works on any actor, with or without a graph. |
| `clear` | Clears static values. |
| `values` | Every value, by layer and owner. |
| `list` | The actor's morphable shapes; the morph names go to the log. |
| `drivers` | Evaluates the actor's correctives now: swing and twist from rest, distance to each pose, outputs. Works on any actor with 3D. |
| `reload` | Re-reads TRI files and driver files. |
| `correctives [on\|off]` | Turns every actor's corrective drivers on or off; toggles without an argument. Other morphs keep applying. Not saved. |
| `smooth [on\|off]` | Turns corrective smoothing on or off; toggles without an argument. Not saved. |
| `smooth passes <count>` | Runtime passes (0 to 400, default 100). |
| `smooth repeat <count>` | Precomputed passes (default 700). Rebuilds every mesh's data. |
| `smooth pin <on\|off>` | Whether open edges keep plain skinning (default on). Rebuilds every mesh's data. |
| `smooth region <hips\|body\|custom>` | Where smoothing applies (default hips and thighs). |
| `smooth auto <on\|off>` | Auto-smoothing of crumpled creases within the region (default off). |
| `smooth autopasses <count>` | Auto-smoothing's passes (1 to 200, default 60). |

The same settings, and the correctives switch, are in the F6 menu's **Settings** tab.

## Not done yet

- Morph tracks in NAF animations and Studio (the animation layer), Studio's morph panel, and capturing a driver pose from Studio.
- An API for other plugins (F4SE messaging), Papyrus, saving static values.
- A memory limit on loaded TRI files.
- A cap on how many actors get correctives, since each morphed actor gets its own copy of every morphed mesh's vertices.
- **Not yet verified in game:** that every animation update (vanilla and NAF) has finished writing node locals by `Main::DrawWorld_PreRender`, where correctives read them. The animation jobs and the draw are synchronized through thread stages that haven't been traced. (The pose rotation axes were confirmed in game: the side-split driver reached about 0.9 in a side split.)
