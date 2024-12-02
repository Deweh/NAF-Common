#pragma once
#include "PNode.h"

namespace Animation::Procedural
{
	class SpringPhysicsJob
	{
	public:
		static constexpr float FIXED_TIMESTEP{ 1.0f / 60.0f };
		static constexpr uint8_t MAX_STEPS_PER_RUN{ 4 };

		template <typename T>
		struct PhysicsTransform
		{
			T current;
			T previous;
		};

		struct Context
		{
			PhysicsTransform<ozz::math::SimdFloat4> position;
			PhysicsTransform<ozz::math::SimdQuaternion> rotation;
			ozz::math::SimdFloat4 angularVelocity;
			ozz::math::SimdFloat4 accumulatedMovement;
			ozz::math::SimdFloat4 prevRootVelocity;
			float deltaTime = 1.0f;
			float accumulatedTime = 0.0f;
			float movementTime = 0.0f;
			bool initialized = false;
		};

		float stiffness;
		float damping;
		float mass;
		float centerOfMassOffsetSq;
		ozz::math::SimdFloat4 gravity;
		ozz::math::SimdFloat4 upAxis;
		const ozz::math::Float4x4* boneTransform;    // Model-space transform.
		const ozz::math::Float4x4* parentTransform;  // Model-space transform.
		const ozz::math::Float4x4* rootTransform;    // World-space transform.
		ozz::math::SimdFloat4* prevRootPos;          // World-space transform.
		Context* context;

		ozz::math::SimdFloat4* positionOutput;  // Local-space transform.
		ozz::math::SimdQuaternion* rotationOutput; // Local-space transform.

		bool Run();

	private:
		struct SubStepConstants
		{
			ozz::math::SimdFloat4 force;
			ozz::math::SimdFloat4 restPositionMS;
			ozz::math::SimdQuaternion restRotationMS;
			ozz::math::SimdFloat4 linearDamping;
			ozz::math::SimdFloat4 angularDamping;
			float massInverse;
		};

		void BeginStepUpdate(SubStepConstants& a_constantsOut);
		void ProcessPhysicsStep(const SubStepConstants& a_constants);
		void ProcessLinearStep(const SubStepConstants& a_constants);
		void ProcessAngularStep(const SubStepConstants& a_constants);
	};

	class PSpringBoneNode : public PNodeT<PSpringBoneNode>
	{
	public:
		struct InstanceData : public PNodeInstanceDataT<InstanceData>
		{
			SpringPhysicsJob::Context context;
		};

		uint16_t boneIdx;
		uint16_t parentIdx;
		ozz::math::Float3 upAxis;
		bool isLinear;
		bool isAngular;

		virtual std::unique_ptr<PNodeInstanceData> CreateInstanceData() override;
		virtual PEvaluationResult Evaluate(PNodeInstanceData* a_instanceData, PoseCache& a_poseCache, PEvaluationContext& a_evalContext) override;
		virtual void AdvanceTime(PNodeInstanceData* a_instanceData, float a_deltaTime) override;
		virtual bool SetCustomValues(const std::span<PEvaluationResult>& a_values, const std::string_view a_skeleton) override;

		inline static Registration _reg{
			"spring_bone",
			{
				{ "pose", PEvaluationType<PoseCache::Handle> },
				{ "stiffness", PEvaluationType<float> },
				{ "damping", PEvaluationType<float> },
				{ "mass", PEvaluationType<float> },
				{ "gravity", PEvaluationType<ozz::math::Float4> },
			},
			{
				{ "bone", PEvaluationType<RE::BSFixedString> },
				{ "up_x", PEvaluationType<float> },
				{ "up_y", PEvaluationType<float> },
				{ "up_z", PEvaluationType<float> },
				{ "linear", PEvaluationType<bool> },
				{ "angular", PEvaluationType<bool> },
			},
			PEvaluationType<PoseCache::Handle>,
			CreateNodeOfType<PSpringBoneNode>
		};
	};
}