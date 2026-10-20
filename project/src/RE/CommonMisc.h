#pragma once
#include "Util/Math.h"

namespace RE
{
#ifdef TARGET_GAME_F4
	struct hkQsTransformf
	{
		hkVector4f translation;
		hkVector4f rotation;
		hkVector4f scale;
	};
	static_assert(sizeof(hkQsTransformf) == 0x30);

	struct BSFaceGenAnimationData :
		public NiExtraData
	{
		enum BlinkState : std::int32_t
		{
			kPreBlink,
			kBlinkDown,
			kBlinkUp,
			kNoBlink_LookingDown,
			kBlinkDead,
			kPreDead,
			kTotal
		};

		enum LipSyncState : std::int32_t
		{
			kNone,
			kPlaying,
			kEnding,
			kNoAnim
		};

		float finalExpression[FACE_MORPHS_SIZE];
		float speechExpression[FACE_MORPHS_SIZE];
		float emotionExpression[FACE_MORPHS_SIZE];
		float speechTimer;
		std::uint32_t faceEmotionInstanceHandle;
		BlinkState blinkState;
		float blinkTimer;
		float lidsFollowEyes;
		BSSpinLock lipSyncLock;
		void* lipSyncAnimHandle;
		BSSoundHandle soundHandle;
		BSFixedString faceAnimArchetype;
		bool dirty;
		bool forceUpdate;
		bool skipUpdate;
		bool actorIsDead;
		SEX gender;
		LipSyncState lipSyncState;
	};
	static_assert(sizeof(BSFaceGenAnimationData) == 0x2E8);

	class alignas(8) bhkThreadMemoryRouter :
		public BSIntrusiveRefCounted
	{
	private:
		inline void ctor(const RE::BSFixedString& a_name, std::uint32_t a_stackSize, bool a_skipInit, std::int32_t a_flags)
		{
			using func_t = decltype(&bhkThreadMemoryRouter::ctor);
			REL::Relocation<func_t> func{ REL::ID(414776) };
			func(this, a_name, a_stackSize, a_skipInit, a_flags);
		}

		inline void dtor()
		{
			using func_t = decltype(&bhkThreadMemoryRouter::dtor);
			REL::Relocation<func_t> func{ REL::ID(55645) };
			func(this);
		}

	public:
		inline bhkThreadMemoryRouter(const RE::BSFixedString& a_name, std::uint32_t a_stackSize = 0x3200, bool a_skipInit = false, std::int32_t a_flags = 0x3)
		{
			ctor(a_name, a_stackSize, a_skipInit, a_flags);
		}

		inline ~bhkThreadMemoryRouter()
		{
			dtor();
		}

		std::byte pad[0x260 - 0x4];
	};
	static_assert(sizeof(bhkThreadMemoryRouter) == 0x260);

	class hkbWorld;
	class hkbCharacterSetup;

	class hkbCharacter :
		public hkReferencedObject
	{
	public:
		hkArray<hkbCharacter*> otherChars;
		std::uint64_t unk20;
		std::int16_t unk28;
		std::int16_t unk2A;
		void* unk30;
		const char* name;
		void* unk40[7];
		hkRefPtr<hkbCharacterSetup> setup;
		void* behaviorGraph;
		void* unk88[3];
		hkbWorld* world;
		hkArray<void*> unkA8;
		void* unkB8[3];
		std::uint64_t flags;
	};
	static_assert(sizeof(hkbCharacter) == 0xD8);

	struct BSFlattenedBoneRef
	{
		NiPointer<NiAVObject> bone;
		std::int32_t idx;
	};

	struct BSAnimationUpdateData
	{
		float timeDelta;
		NiPoint3 unkPos;
		IPostAnimationChannelUpdateFunctor* postUpdateFunctor;
		std::int16_t boneCount;
		bool forceUpdate;
		bool unkFlag1;
		bool visible;
		bool unkFlag2;
	};
	static_assert(sizeof(BSAnimationUpdateData) == 0x20);

	__declspec(novtable) class BSIRagdollDriver
	{
	public:
		virtual ~BSIRagdollDriver() = default;
		virtual bool HasRagdollImpl();
		virtual bool GetWroteTransformToCharacterImpl();
		virtual bool AddRagdollToPhysicsWorldImpl();
		virtual bool RemoveRagdollFromPhysicsWorldImpl();
		virtual void SetPhysicsWorldImpl(bhkWorld*);
		virtual void ResetRagdollImpl();
		virtual void InitRagdollImpl();
		virtual void UpdateRagdollConstraintsImpl();
		virtual void ChangeMotionOnAttachmentsImpl(hknpMotionPropertiesId::Preset);
		virtual void RemoveAttachmentsFromWorldImpl();
		virtual void RestoreOriginalMassInRagdollImpl();
		virtual void SetRagdollTargetSyncImpl(bool);
		virtual void EaseInConstraintsImpl(float);
		virtual void SetConstraintsEnabledForUnlockedBonesImpl(bool);
		virtual void UpdateRagdollConstraintStateImpl();
		virtual void GetRagdollPoseAABBImpl(hkAabb*);
		virtual void GetRagdollPhysicsWorldImpl(hknpBSWorld**, bhkWorld**);
		virtual void RagdollInstanceCreatedImpl(hknpBSWorld*);
		virtual bool IsPhysicsDrivenImpl();
		virtual void ReleaseRagdollImpl();
		virtual void SetRagdollInWorldCacheValueImpl(bool);
	};

	class BShkbAnimationGraph :
		public BSIRagdollDriver,
		public BSIntrusiveRefCounted,
		public BSTEventSource<BSActiveGraphIfInactiveEvent>,
		public BSTEventSource<BSAnimationGraphEvent>,
		public BSTEventSource<BSMovementDataChangedEvent>,
		public BSTEventSource<BSSubGraphActivationUpdate>,
		public BSTEventSource<BSTransformDeltaEvent>
	{
	public:
		hkbCharacter character;
		BSTArray<BSFlattenedBoneRef> bones;
		std::uint32_t unk2B8[38];
		float times[2];
		BSSpinLock swapDataLock;
		BSFixedString name;
		void* projHandle;
		void* projDBData;
		void* behaviorGraph;
		TESObjectREFR* managedRef;
		NiPointer<NiAVObject> skeletonRootNode;
		void* unk390[3];
		float deltaTime;
		float weight;
		float prevWeight;
		bhkWorld* world;
		std::int16_t boneCount;
		bool unkFlag1;
		bool loading;
		bool active;
		bool doUpdate;
		bool doGenerate;
		bool useBoneLOD;
		bool controlsGamebryoAnim;
		bool unkFlag2;
		bool unkFlag3;
	};
	static_assert(sizeof(BShkbAnimationGraph) == 0x3D0);
#endif

#ifdef TARGET_GAME_SF
	class BSLight;
	class BSGeometry;
	class BSFaceGenNiNode;

	// An NiExtraData named "FAD" (vtable 427138, ctor 69513, dtor 69514, size 0x3D0), held by BSFaceGenNiNode+0x180.
	// Update (69533) is called once per frame by the face-gen node's update (69580, call at +0x179), which only re-applies
	// the morphs to the geometry when Update returns true (or node flag 0x100 is set). Update, under lipSyncLock:
	//  1. blinks (69530): writes finalExpression[25/26] (eyeClosedL/R),
	//  2. clears speechExpression[0..100] and fills it from the FaceFX lip-sync tracks,
	//  3. applies the equipment flags (69535): [101..103] of both arrays, and bit 2 forces speechExpression[25/26] to 1,
	//  4. when anything changed, finalExpression[0..100] = clamp(speechExpression, -1, 1),
	//  5. final[25/26] = min(max(speech, blink), fFGBlinkClosedValue); final[27/28/31/32] += the eyeOffset* fields,
	//  6. rebuilds the corrective (combination) morphs from the other final values:
	//     [10..17] c_eye<Dir>_eyeClosedL/R = max(final Look<Dir> [97..100], lid-follow weight) * final eyeClosedL/R,
	//              lid-follow weight = clamp(eyeOffset<Dir> / fLidsFollowingEyes<Dir>Mult * 2.5, 0, 1),
	//     [18/19]  c_eyesClosed50L/R = triangle of eyeClosedL/R peaking at 0.5 (2v below 0.5, 2 - 2v above),
	//     [21/22]  c_squintL/R_cheekRaiserL/R = squintL/R [76/77] * cheekRaiseL/R [4/5].
	// So [10..19] and [21/22] are overwritten from the other morphs every update that runs step 4.
	// Morph names are the 104-entry table at ID 930506 ({BSFixedString, bool} each).
	class BSFaceGenAnimationData : public NiRefObject
	{
	public:
		enum BlinkState : std::uint32_t
		{
			kPreBlink,   // counting blinkTimer down to the next blink
			kBlinkDown,  // fBlinkDownTime
			kBlinkHold,  // fBlinkHoldTime
			kBlinkUp,    // fBlinkUpTime
			kReset,      // picks a new blink delay, back to kPreBlink
			// 5 and 6: eyes held closed while skipUpdate is set, 6 -> 5 when the timer runs out.
		};

		enum LipSyncState : std::uint32_t
		{
			kNone,
			kPlaying,
			kEnding,  // set when soundHandle stops being valid
			kNoAnim   // initial value
		};

		// Bits of equipmentFlags. Set through 69534 (bit index, bool).
		enum EquipmentFlag : std::uint8_t
		{
			kHat = 1 << 0,         // [101] Hat
			kHideEar = 1 << 1,     // [102] HideEar
			kEyesClosed = 1 << 2,  // forces speechExpression[25/26] (eyeClosedL/R) to 1; set by the face setup (40927)
			                       // from ref vfunc 0x868 or ref flag 0x200 (probably "is dead")
			kMask = 1 << 3         // [103] Mask
		};

		// 00C is a u32 (0xFFFFFFFF) inside NiRefObject's padding.
		BSFixedString name;                              // 010 - NiExtraData, "FAD"
		float         finalExpression[FACE_MORPHS_SIZE];  // 018 - output, read by the face-gen node
		float         speechExpression[FACE_MORPHS_SIZE];  // 1B8 - input, rebuilt every Update; [0..100] from FaceFX
		float*        expressionOverride;                 // 358 - float[101], heap. Photo Mode's "Expression" preset: set by
		                                                  //       69527 via Actor 100543, freed by Actor 100544. Reader not found;
		                                                  //       Update and the node update don't read it.
		float         lipSyncExtraChannels[3];            // 360 - FaceFX tracks 101..103: Blink, Eyebrow Raise, Squint
		float         speechTimer;                        // 36C
		std::uint32_t faceEmotionInstanceHandle;          // 370 - slot in a 64-entry pool; 0xFFFFFFFF = none
		std::uint32_t unk374;                             // 374 - a reference handle, cleared once its 3D is ready
		bool          equipmentFlagsChanged;              // 378
		std::uint8_t  equipmentFlags;                     // 379 - EquipmentFlag
		std::uint8_t  lastEquipmentFlags;                 // 37A
		std::uint8_t  pad37B;                             // 37B
		BlinkState    blinkState;                         // 37C
		float         blinkTimer;                         // 380 - starts at rand(fBlinkDelayMin, fBlinkDelayMax)
		// Lids following the eyes: written by eye tracking (81967) as eye angle * fLidsFollowingEyes<Dir>Mult, which also
		// sets forceUpdate. Only one of up/down and one of left/right is non-zero.
		float         eyeOffsetUp;                        // 384 - added to [32] eyeUp
		float         eyeOffsetDown;                      // 388 - added to [27] eyeDown
		float         eyeOffsetLeft;                      // 38C - added to [28] eyeLeft
		float         eyeOffsetRight;                     // 390 - added to [31] eyeRight
		float         lookUp;                             // 394 - added to speechExpression[98] LookUp
		float         lookDown;                           // 398 - added to speechExpression[97] LookDown
		float         lookLeft;                           // 39C - added to speechExpression[100] LookLeft
		float         lookRight;                          // 3A0 - added to speechExpression[99] LookRight
		std::uint32_t lipSyncLock[2];                     // 3A4 - recursive lock; locked by 123819
		std::uint32_t soundHandle;                        // 3AC - voice line sound ID; 0 = none
		BSFixedString faceAnimArchetype;                  // 3B0
		bool          dirty;                              // 3B8 - Update's return value
		bool          forceUpdate;                        // 3B9 - the node updates even when not asked to; cleared
		bool          skipUpdate;                         // 3BA - Update does nothing while set
		std::uint8_t  pad3BB;                             // 3BB
		std::uint32_t gender;                             // 3BC - 1 = female; from BSFaceGenNiNode flag 0x2000
		void*         lipSyncAnim;                        // 3C0 - refcounted FaceFX actor instance (+0x8 refcount)
		std::uint32_t actorHandle;                        // 3C8 - handle of the owning reference, set by the face 3D setup
		                                                  //       (40933) right after it creates this data; 0 before that.
		                                                  //       Update resets it to the invalid handle if it stops resolving.
		LipSyncState  lipSyncState;                       // 3CC
	};
	static_assert(offsetof(BSFaceGenAnimationData, finalExpression) == 0x18);
	static_assert(offsetof(BSFaceGenAnimationData, speechExpression) == 0x1B8);
	static_assert(offsetof(BSFaceGenAnimationData, expressionOverride) == 0x358);
	static_assert(offsetof(BSFaceGenAnimationData, blinkState) == 0x37C);
	static_assert(offsetof(BSFaceGenAnimationData, faceAnimArchetype) == 0x3B0);
	static_assert(offsetof(BSFaceGenAnimationData, lipSyncAnim) == 0x3C0);
	static_assert(sizeof(BSFaceGenAnimationData) == 0x3D0);

	// BSAnimationUpdateData, BGSModelNode and NiNode come from CommonLibSF.

	template <class T>
	struct BSArray
	{
		uint32_t size;
		uint32_t capacity;
		T* data;
	};

	// BSModelNode::faceGenNodes (+0x48), verified for 1.16.
	inline BSArray<BSFaceGenNiNode*>& GetFaceGenNodes(BGSModelNode* a_modelNode)
	{
		return *reinterpret_cast<BSArray<BSFaceGenNiNode*>*>(reinterpret_cast<std::uintptr_t>(a_modelNode) + 0x48);
	}

	// A model's rig keeps its own copy of each node's local transform (Rig::local, one NiTransform per slot) and builds the
	// world transforms from it every frame (BSModelNode vfunc 0x2, ID 147429). NiAVObject::local is only read when the rig is
	// built, so the game copies a node's new local into its slot when it moves one (ID 68200, slot lookup ID 147430).
	// Rig::rebuild makes the next update tear the rig down and build a new one from the scene graph (ID 147424). The game sets
	// it when a node is attached or detached under the model.
	namespace ModelRig
	{
		using Rig = BGSModelNode::Rig;

		inline constexpr std::uint16_t kInvalidSlot = 0x3FF;

		// Entry of the rig's node name -> slot hash map at Rig+0x48, which holds every node in the model.
		struct SlotMapEntry
		{
			const void*   name;   // 00 - BSFixedString handle
			std::uint64_t value;  // 08 - low 10 bits: slot
			std::uint32_t next;   // 10 - 0xFFFFFFFF: empty
			std::uint32_t unk14;  // 14
		};
		static_assert(sizeof(SlotMapEntry) == 0x18);

		template <class F>
		void ForEachSlot(Rig* a_rig, F&& a_func)
		{
			const auto base = reinterpret_cast<std::uintptr_t>(a_rig);
			const auto entries = *reinterpret_cast<SlotMapEntry**>(base + 0x58);
			const auto capacity = *reinterpret_cast<std::uint64_t*>(base + 0x60);
			if (!entries)
				return;
			for (std::uint64_t i = 0; i < capacity; i++) {
				if (entries[i].next != 0xFFFFFFFF && entries[i].name)
					a_func(entries[i].name, static_cast<std::uint16_t>(entries[i].value & kInvalidSlot));
			}
		}

		// The handle the slot map is keyed by.
		inline const void* GetNameHandle(const NiAVObject* a_node)
		{
			static_assert(sizeof(BSFixedString) == sizeof(void*));
			return *reinterpret_cast<const void* const*>(&a_node->name);
		}

		// Rig::Buffer is { refCount, size (+0x8), capacity (+0xC), data (+0x10) }.
		inline std::uint32_t GetSize(const Rig::Buffer* a_buffer)
		{
			return *reinterpret_cast<const std::uint32_t*>(reinterpret_cast<std::uintptr_t>(a_buffer) + 0x8);
		}

		inline void AddRef(Rig* a_rig)
		{
			std::atomic_ref(a_rig->refCount).fetch_add(1);
		}

		// Only calls into the game to free the rig when this is the last reference.
		inline void Release(Rig* a_rig)
		{
			std::atomic_ref refCount(a_rig->refCount);
			for (auto count = refCount.load(); count > 1;) {
				if (refCount.compare_exchange_weak(count, count - 1))
					return;
			}
			REL::Relocation<void(Rig*)> func{ REL::ID(68279) };
			func(a_rig);
		}
	}

	// CommonLibSF's ID::Main::WorldRoot is still 0 for 1.16, and looking up 0 shows an error box (hidden behind the game,
	// which then looks frozen). Built by 0x14083e9a0 ("World"): its cameraRoot (+0x78) and camera (+0x80) match SceneGraph.
	inline SceneGraph* GetWorldRoot()
	{
		static REL::Relocation<SceneGraph**> worldRoot{ REL::ID(936470) };
		return *worldRoot;
	}

	// CommonLibSF's ProcessLists::highActorHandles (+0x58) is 8 bytes early for 1.16. The constructor (0x141a88ca0) builds the
	// four actor handle arrays at +0x60, +0x70, +0x80 and +0x90, and the game iterates the high process actors from +0x60
	// (e.g. 0x140d134b0).
	inline BSTArray<BSPointerHandle<Actor>>& GetHighActorHandles(ProcessLists* a_processLists)
	{
		return *reinterpret_cast<BSTArray<BSPointerHandle<Actor>>*>(reinterpret_cast<std::uintptr_t>(a_processLists) + 0x60);
	}

	// The race an actor's animation graph is made from: TESObjectREFR vfunc 0x580 (Actor's is 0x1418eea70). A race
	// override extra (type 0xB7) first, then the actor's race, then its base NPC's. Its animation graph component, and so
	// its .rig, is picked from this race when the actor's graph manager is made (Actor vfunc 0x570, 0x1418ee8e0).
	inline TESRace* GetAnimationRace(Actor* a_actor)
	{
		using func_t = TESRace* (*)(Actor*);
		return reinterpret_cast<func_t>((*reinterpret_cast<void***>(a_actor))[0x580 / 8])(a_actor);
	}

	// What the SetPos (opcode 0x1007) and SetAngle (0x1009) console commands call (handlers 0x140c23830, 0x140c24260). Off the
	// main thread a request is queued; on it, a position request (0x1418c9b90) sets the reference's position, its character
	// controller's (vfunc 0x240) and its 3D's local translate.
	struct TransformsManager
	{
		static TransformsManager* GetSingleton()
		{
			static REL::Relocation<TransformsManager**> singleton{ REL::ID(883606) };
			return *singleton;
		}

		// a_axis is 'X', 'Y' or 'Z'. Angles are in degrees.
		void RequestTransformUpdate(std::uint32_t a_opcode, TESObjectREFR* a_refr, char a_axis, double a_value)
		{
			using func_t = decltype(&TransformsManager::RequestTransformUpdate);
			static REL::Relocation<func_t> func{ REL::ID(99963) };
			func(this, a_opcode, a_refr, a_axis, a_value);
		}

		void RequestPositionUpdate(TESObjectREFR* a_refr, const NiPoint3& a_position)
		{
			RequestTransformUpdate(0x1007, a_refr, 'X', a_position.x);
			RequestTransformUpdate(0x1007, a_refr, 'Y', a_position.y);
			RequestTransformUpdate(0x1007, a_refr, 'Z', a_position.z);
		}

		void RequestRotationUpdate(TESObjectREFR* a_refr, const NiPoint3& a_rotation)
		{
			RequestTransformUpdate(0x1009, a_refr, 'X', a_rotation.x * Util::RADIAN_TO_DEGREE);
			RequestTransformUpdate(0x1009, a_refr, 'Y', a_rotation.y * Util::RADIAN_TO_DEGREE);
			RequestTransformUpdate(0x1009, a_refr, 'Z', a_rotation.z * Util::RADIAN_TO_DEGREE);
		}

		void RequestPosRotUpdate(TESObjectREFR* a_refr, const NiPoint3& a_position, const NiPoint3& a_rotation)
		{
			RequestPositionUpdate(a_refr, a_position);
			RequestRotationUpdate(a_refr, a_rotation);
		}
	};

	// Starfield's fade node is BSFadeNode; CommonLibSF maps it for the current game.
	using BGSFadeNode = BSFadeNode;

	class BSGeometry : public NiAVObject
	{
	public:
		virtual ~BSGeometry() = default;

		void* unk130;
		void* unk138;
		void* unk140;
		void* unk148;
		void* unk150;
		void* unk158;
		void* unk160;
		void* unk168;
		void* unk170;
		void* unk178;
		void* unk180;
		void* unk188;
		void* unk190;
		void* unk198;
		void* unk1A0;
		void* unk1A8;
		void* skinInstance;
		void* morphTargetData;
		void* unk1C0;
		void* unk1C8;
		BSFixedString materialPath;
	};

	class BSFaceGenNiNode : public NiNode
	{
	public:
		virtual ~BSFaceGenNiNode() = default;
		NiMatrix3 unk150;
		BSFaceGenAnimationData* faceGenAnimData;  //180
		float unk188;
		uint32_t facegenFlags;
	};

	struct BSModelDBEntry
	{
		void* unk00;
		void* unk08;
		void* unk10;
		void* unk18;
		BGSFadeNode* node;
	};

	class hkVector4f
	{
	public:
		__m128 quad;  // 00
	};
	static_assert(sizeof(hkVector4f) == 0x10);

	class bhkCharacterController
	{
	public:
		virtual ~bhkCharacterController();

		virtual void Unk_01();                                                        // 01
		virtual void Unk_02();                                                        // 02
		virtual void Unk_03();                                                        // 03
		virtual void Unk_04();                                                        // 04
		virtual void Unk_05();                                                        // 05
		virtual void Unk_06();                                                        // 06
		virtual void Unk_07();                                                        // 07
		virtual void Unk_08();                                                        // 08
		virtual void Unk_09();                                                        // 09
		virtual void Unk_0A();                                                        // 0A
		virtual void Unk_0B();                                                        // 0B
		virtual void Unk_0C();                                                        // 0C
		virtual void Unk_0D();                                                        // 0D
		virtual void Unk_0E();                                                        // 0E
		virtual void Unk_0F();                                                        // 0F
		virtual void Unk_10();                                                        // 10
		virtual void Unk_11();                                                        // 11
		virtual void Unk_12();                                                        // 12
		virtual void Unk_13();                                                        // 13
		virtual void Unk_14();                                                        // 14
		virtual void Unk_15();                                                        // 15
		virtual void Unk_16();                                                        // 16
		virtual void Unk_17();                                                        // 17
		virtual void Unk_18();                                                        // 18
		virtual void Unk_19();                                                        // 19
		virtual void Unk_1A();                                                        // 1A
		virtual void Unk_1B();                                                        // 1B
		virtual void Unk_1C();                                                        // 1C
		virtual void Unk_1D();                                                        // 1D
		virtual void Unk_1E();                                                        // 1E
		virtual void Unk_1F();                                                        // 1F
		virtual void Unk_20();                                                        // 20
		virtual void Unk_21();                                                        // 21
		virtual void Unk_22();                                                        // 22
		virtual void Unk_23();                                                        // 23
		virtual void Unk_24();                                                        // 24
		virtual void Unk_25();                                                        // 25
		virtual void Unk_26();                                                        // 26
		virtual void Unk_27();                                                        // 27
		virtual void Unk_28();                                                        // 28
		virtual void Unk_29();                                                        // 29
		virtual void Unk_2A();                                                        // 2A
		virtual void Unk_2B();                                                        // 2B
		virtual void Unk_2C();                                                        // 2C
		virtual void Unk_2D();                                                        // 2D
		virtual void Unk_2E();                                                        // 2E
		virtual void Unk_2F();                                                        // 2F
		virtual void Unk_30();                                                        // 30
		virtual void Unk_31();                                                        // 31
		virtual void Unk_32();                                                        // 32
		virtual void Unk_33();                                                        // 33
		virtual void Unk_34();                                                        // 34
		virtual void Unk_35();                                                        // 35
		virtual void Unk_36();                                                        // 36
		virtual void Unk_37();                                                        // 37
		virtual void Unk_38();                                                        // 38
		virtual void Unk_39();                                                        // 39
		virtual void Unk_3A();                                                        // 3A
		virtual void Unk_3B();                                                        // 3B
		virtual void Unk_3C();                                                        // 3C
		virtual void Unk_3D();                                                        // 3D
		virtual void Unk_3E();                                                        // 3E
		virtual void Unk_3F();                                                        // 3F
		virtual void Unk_40();                                                        // 40
		virtual void Unk_41();                                                        // 41
		virtual void Unk_42();                                                        // 42
		virtual void Unk_43();                                                        // 43
		virtual void Unk_44();                                                        // 44
		virtual void Unk_45();                                                        // 45
		virtual void Unk_46();                                                        // 46
		virtual void SetPosition(const RE::NiPoint3A& a_pos, bool a_unkFlag = true);  // 47
		virtual void Unk_48();                                                        // 48
		virtual void Unk_49();                                                        // 49
		virtual void Unk_4A();                                                        // 4A
		virtual void Unk_4B();                                                        // 4B
		virtual void Unk_4C();                                                        // 4C
		virtual void Unk_4D();                                                        // 4D
		virtual void* Unk_4E();                                                       // 4E
		virtual void* SetLinearVelocityImpl(const hkVector4f& a_velocity);            // 4F
		virtual void Unk_50();                                                        // 50
		virtual void Unk_51();                                                        // 51
		virtual void Unk_52();                                                        // 52
		virtual void Unk_53();                                                        // 53
		virtual void Unk_54();                                                        // 54
		virtual void Unk_55();                                                        // 55
		virtual void Unk_56();                                                        // 56
		virtual void Unk_57();                                                        // 57
		virtual void Unk_58();                                                        // 58
		virtual void Unk_59();                                                        // 59
		virtual void Unk_5A();                                                        // 5A
		virtual void Unk_5B();                                                        // 5B
		virtual void Unk_5C();                                                        // 5C
		virtual void Unk_5D();                                                        // 5D
		virtual void Unk_5E();                                                        // 5E
		virtual void Unk_5F();                                                        // 5F
		virtual void Unk_60();                                                        // 60
	};

	namespace ModelDB
	{
		// BSResource2 entry for BSModelDB. Refcounted; vfunc 1 releases it.
		struct Entry
		{
			virtual ~Entry() = default;
			virtual void Release();  // 01

			uint64_t unk08;
			uint32_t unk10;
			uint32_t state;  // 14: bits 0x70000000 hold the load state
			uint64_t unk18;
			NiAVObject* root;  // 20

			bool IsLoaded() const
			{
				const uint32_t s = state & 0x70000000;
				return s == 0x30000000 || s == 0x40000000;
			}
		};

		struct DemandArgs
		{
			uint32_t unk00 = 3;    // from 0x140583230: 3 for NPC_, 2 for most placeable objects, 1 otherwise
			uint8_t flags = 0x3D;  // 0x2D | (isBoundObject << 4)
			uint8_t unk05 = 0;     // bit 0 cleared by every caller found
		};

		struct Handle
		{
			Entry* entry = nullptr;

			Handle() = default;
			Handle(const Handle&) = delete;
			Handle& operator=(const Handle&) = delete;
			Handle(Handle&& a_other) noexcept : entry(std::exchange(a_other.entry, nullptr)) {}
			~Handle()
			{
				if (entry)
					entry->Release();
			}

			NiAVObject* get() const { return entry && entry->IsLoaded() ? entry->root : nullptr; }
		};

		// BSModelDB::Demand(path, handle, args): blocking load of a model, path relative to "meshes\".
		// This is what the reference 3D loader (0x140a71710) uses for an actor's skeleton.
		inline Handle Demand(const char* a_path)
		{
			Handle result;
			if (!a_path || !*a_path)
				return result;

			DemandArgs args;
			REL::Relocation<int (*)(const char*, Entry**, DemandArgs*)> func{ REL::ID(127172) };  // 0x142429820
			func(a_path, &result.entry, &args);  // 0 on success; Handle::get() checks the entry's state either way
			return result;
		}
	}

	// PlayerCamera's free camera, driven the way the `tfc` console command (0x1409fa6a0) drives it.
	namespace FreeCamera
	{
		inline constexpr std::uint32_t kStateIndex = CameraStates::kFreeWalk;  // What `tfc` with no argument uses.
		inline constexpr std::uint32_t kOff = CameraStates::kTotal;             // Passing this to ToggleFreeCameraMode turns it off.

		// FreeWalkCameraState. Its update (vfunc 0xC) adds look and move input to these, then sets the camera root's
		// local transform from them, so writing them moves the camera at the game's next camera update.
		struct State
		{
			std::byte pad00[0x70];
			NiPoint3 translation;  // 70: vfuncs 0xE / 0x10 get and set it
			float pitch;           // 7C: radians, positive looks down. The game wraps it into [0, 2pi).
			float yaw;             // 80: radians, clockwise from +Y. Also wrapped into [0, 2pi).
		};

		// Set by ToggleFreeCameraMode while the free camera is on.
		inline bool IsActive(PlayerCamera* a_camera) { return *reinterpret_cast<bool*>(reinterpret_cast<std::uintptr_t>(a_camera) + 0x2E5); }
		// While set, `tfc` does nothing, and ToggleFreeCameraMode won't switch to a free camera state.
		inline bool IsBlocked(PlayerCamera* a_camera) { return *reinterpret_cast<bool*>(reinterpret_cast<std::uintptr_t>(a_camera) + 0x2DD); }

		inline State* GetState(PlayerCamera* a_camera)
		{
			if (!a_camera || !IsActive(a_camera) || !a_camera->QCameraEquals(static_cast<CameraState>(kStateIndex)))
				return nullptr;
			return static_cast<State*>(a_camera->currentState);
		}
	}

	namespace EyeTracking
	{
		struct EyeData
		{
			NiPointer<NiNode> head;
			NiPointer<NiNode> leftEye;
			NiPointer<NiNode> rightEye;
			NiPointer<NiNode> eyeTarget;
		};

		struct EyeNodes
		{
			inline EyeNodes()
			{
				REL::Relocation<uint64_t*> eyeTargetMutex(REL::ID(858265));
				REL::Relocation<void(uint64_t*)> LockWrite(REL::ID(34125));
				mutex = eyeTargetMutex.get();
				LockWrite(mutex);
			}

			inline EyeNodes(const EyeNodes& other) = delete;

			inline EyeNodes(EyeNodes&& other) noexcept
			{
				data = other.data;
				mutex = other.mutex;
				other.data = std::span<EyeData>();
				other.mutex = nullptr;
			}

			inline ~EyeNodes()
			{
				unlock();
			}

			inline void unlock()
			{
				if (mutex) {
					REL::Relocation<void(uint64_t**)> UnlockWrite(REL::ID(36740));
					UnlockWrite(&mutex);
				}
			}

			uint64_t* mutex = nullptr;
			std::span<EyeData> data;
		};

		// TODO: the eye-target lock (old ID 858265) and node array (old ID 858268, not in the 1.16 Address Library) moved in
		// 1.16; constructing EyeNodes would look them up. Until they're found again there are no eye nodes to redirect.
		inline std::optional<EyeNodes> GetEyeNodes()
		{
			return std::nullopt;
		}
	}
#endif
}