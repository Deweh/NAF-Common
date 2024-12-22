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

	class BSFaceGenAnimationData
	{
	public:
		virtual ~BSFaceGenAnimationData() = default;

		void* unk08;
		void* unk10;
		float finalExpression[FACE_MORPHS_SIZE];
		float expression2[FACE_MORPHS_SIZE];
		void* unk358;
		void* unk360;
		void* unk368;
		void* unk370;
		float unk378;
		uint32_t unk37C;
		void* unk380;
		void* unk388;
		void* unk390;
		void* unk398;
		void* unk3A0;
		BSFixedString animFaceArchetype;
		uint32_t unk3B0;
		uint32_t unk3B4;
		void* facefx_actor;
		uint32_t unk3C0;
		uint32_t unk3C4;
		void* unk3C8;
		void* unk3D0;
		void* unk3D8;
		void* unk3E0;
		void* unk3E8;
		BSFixedString morphNames[FACE_MORPHS_SIZE];
	};

	struct BSAnimationUpdateData
	{
		NiPoint3A rootLocation1;
		NiPoint3A rootAngle1;
		NiPoint3A rootLocation2;
		NiPoint3A rootAngle2;
		float unk01;
		float unk02;
		IPostAnimationChannelUpdateFunctor* postUpdateFunctor;
		float unk05;
		float unk06;
		float unk07;
		float unk08;
		float timeDelta;
		float unk09;
		uint16_t unk11;
		bool forceUpdate;
		bool modelCulled;
		bool unk13;
		bool unkFlag;
		bool unk15;
		float unk16;
	};
	static_assert(offsetof(BSAnimationUpdateData, timeDelta) == 0x60);
	static_assert(offsetof(BSAnimationUpdateData, modelCulled) == 0x6B);

	template <class T>
	struct BSArray
	{
		uint32_t size;
		uint32_t capacity;
		T* data;
	};

	class BGSModelNode
	{
	public:
		virtual ~BGSModelNode() = default;
		virtual void* Refresh(NiNode* rootNode, NiUpdateData*);
		struct UpdateData
		{
			NiTransform t1;
			NiTransform t2;
			NiTransform t3;
			NiPoint4 point;
		};
		virtual bool Update(UpdateData* data, NiUpdateData*, NiTransform*);
		uint32_t refcount;
		uint32_t pad0C;

		struct Unk10Struct
		{
			void* unk00_unk88[0x88 >> 3];
			bool needsUpdate;
		};
		Unk10Struct* unk10;

		struct NodeEntry
		{
			uint16_t index;
			uint16_t unk02;
			uint16_t unk04;
			uint16_t unk06;
			NiNode* node;
		};

		BSArray<NodeEntry> nodes;         //18
		BSArray<BSGeometry*> geometries;  //28
		void* unk38;
		void* unk40;
		BSArray<BSFaceGenNiNode*> facegenNodes;  //48
		BSArray<NiNode*> otherNodes;             //58
		void* unk68;
		void* bhkNPModelNodeComponent;
		void* unk78;
		void* unk80;
		void* unk88;
	};
	static_assert(offsetof(BGSModelNode, nodes) == 0x18);

	template <class T>
	class NiTArray
	{
	public:
		virtual ~NiTArray() = default;
		T* entries;
		uint16_t m_arrayBufLen;    // 10 - max elements storable in m_data
		uint16_t m_emptyRunStart;  // 12 - index of beginning of empty slot run
		uint16_t m_size;           // 14 - number of filled slots
		uint16_t m_growSize;       // 16 - number of slots to grow m_data by
	};

	class NiNode : public NiAVObject
	{
	public:
		virtual ~NiNode() = default;
		virtual void* AttachChild(NiAVObject* child, bool firstAvailable);
		virtual void* InsertChildAt(uint32_t idx, NiAVObject* child);
		virtual void* DetachChild(NiAVObject* child);
		virtual void* DetachChildOut(NiAVObject* child, NiAVObject** outObj);  //RE::NiPointer<NiAVObject>&
		virtual void* DetachChildAt(uint32_t idx);
		virtual void* DetachChildAtOut(uint32_t idx, NiAVObject** outObj);  //RE::NiPointer<NiAVObject>&
		virtual void* SetAt(uint32_t idx, NiAVObject* child);
		virtual void* SetAtOut(uint32_t idx, NiAVObject* child, NiAVObject** outObj);  //RE::NiPointer<NiAVObject>&
		virtual void* Unk92();
		virtual void* Unk93();
		virtual void* Unk94();

		NiTArray<NiNode*> children;  //NiTObjectArray<NiPointer<NiAVObject>> //130
		void* unk148;
	};

	class BGSFadeNode : public NiNode
	{
	public:
		virtual ~BGSFadeNode() = default;

		struct UnkEntry
		{
			std::byte unk00[32];
			NiBound worldBound;
			NiAVObject* node;
			void* unk01;
		};

		void* unk150;
		void* unk158;
		void* unk160;
		void* unk168;
		BSArray<UnkEntry> geometries;
		BGSModelNode* bgsModelNode;
	};

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

	struct TransformsManager
	{
		static TransformsManager* GetSingleton()
		{
			REL::Relocation<TransformsManager**> singleton(REL::ID(881086));
			return *singleton;
		}

		void RequestTransformUpdate(uint32_t a_flags, RE::TESObjectREFR* a_refr, char a_axis, double a_value)
		{
			using func_t = decltype(&TransformsManager::RequestTransformUpdate);
			REL::Relocation<func_t> func{ REL::ID(149852) };
			func(this, a_flags, a_refr, a_axis, a_value);
		}

		void RequestPositionUpdate(RE::TESObjectREFR* a_ref, const RE::NiPoint3& a_pos)
		{
			using func_t = decltype(&TransformsManager::RequestTransformUpdate);
			static REL::Relocation<func_t> func{ REL::ID(149852) };
			func(this, 0x1007, a_ref, 'X', a_pos.x);
			func(this, 0x1007, a_ref, 'Y', a_pos.y);
			func(this, 0x1007, a_ref, 'Z', a_pos.z);
		}

		void RequestRotationUpdate(RE::TESObjectREFR* a_ref, const RE::NiPoint3& a_rot)
		{
			using func_t = decltype(&TransformsManager::RequestTransformUpdate);
			REL::Relocation<func_t> func{ REL::ID(149852) };
			func(this, 0x1009, a_ref, 'X', a_rot.x * Util::RADIAN_TO_DEGREE);
			func(this, 0x1009, a_ref, 'Y', a_rot.y * Util::RADIAN_TO_DEGREE);
			func(this, 0x1009, a_ref, 'Z', a_rot.z * Util::RADIAN_TO_DEGREE);
		}

		void RequestPosRotUpdate(RE::TESObjectREFR* a_ref, const RE::NiPoint3& a_pos, const RE::NiPoint3& a_rot)
		{
			using func_t = decltype(&TransformsManager::RequestTransformUpdate);
			REL::Relocation<func_t> func{ REL::ID(149852) };
			func(this, 0x1007, a_ref, 'X', a_pos.x);
			func(this, 0x1007, a_ref, 'Y', a_pos.y);
			func(this, 0x1007, a_ref, 'Z', a_pos.z);
			func(this, 0x1009, a_ref, 'X', a_rot.x * Util::RADIAN_TO_DEGREE);
			func(this, 0x1009, a_ref, 'Y', a_rot.y * Util::RADIAN_TO_DEGREE);
			func(this, 0x1009, a_ref, 'Z', a_rot.z * Util::RADIAN_TO_DEGREE);
		}
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
		virtual void* SetLinearVelocityImpl(const hkVector4f& a_velocity);            // 4E
		virtual void Unk_4F();                                                        // 4F
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
		struct Entry
		{
			void* unk00;
			void* unk08;
			void* unk10;
			void* unk18;
			BGSFadeNode* node;
		};

		inline Entry* GetEntry(const char* filename)
		{
			uint64_t flag = 0x3;
			struct UnkOut
			{
				uint32_t unk00 = 0;
				uint32_t unk04 = 0;
				uint64_t unk08 = 0;
				uint64_t unk10 = 0;
			};
			UnkOut out;
			out.unk00 = flag & 0xFFFFFFu;
			const bool isBound = true;
			out.unk00 |= ((16 * (isBound & 1)) | 0x2D) << 24;
			out.unk04 = 0xFEu;
			Entry* entry = nullptr;
			REL::Relocation<int(const char*, Entry**, UnkOut&)> GetModelDBEntry(REL::ID(183072));
			GetModelDBEntry(filename, &entry, out);
			REL::Relocation<uint64_t(UnkOut&)> DecRef(REL::ID(36741));
			DecRef(out);
			return entry;
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

		inline EyeNodes GetEyeNodes()
		{
			REL::Relocation<EyeData*> eyeTargetNodes(REL::ID(858268));
			//REL::Relocation<uint32_t*> eyeTargetIdx(REL::ID(880003));

			EyeNodes result;
			result.data = std::span<EyeData>(eyeTargetNodes.get(), 200);

			return result;
		}
	}
#endif
}