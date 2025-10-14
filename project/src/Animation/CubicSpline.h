#pragma once
#include "ozz/base/maths/simd_math.h"

namespace ozz
{
	namespace animation
	{

		// Cubic spline interpolation for SIMD quaternions
		// Supports both self-contained and continuous (looping) modes
		// With dynamic velocity scaling for speed-responsive rotation curves
		class QuaternionCubicSpline
		{
		public:
			enum class Mode
			{
				kSelfContained,  // Standard spline, end values determine boundaries
				kContinuous      // Smooth loop from end to start
			};

			// Key frame data structure
			struct KeyFrame
			{
				math::SimdQuaternion rotation;
				float time;

				KeyFrame();
				KeyFrame(const math::SimdQuaternion& q, float t);
			};

			QuaternionCubicSpline();

			// Initialize with keyframes and mode
			void Initialize(const std::vector<KeyFrame>& keyframes, Mode mode = Mode::kSelfContained);

			// Convenience initialization from separate arrays
			void Initialize(const std::vector<math::SimdQuaternion>& rotations,
				const std::vector<float>& times,
				Mode mode = Mode::kSelfContained);

			// Set velocity scale factor
			// scale > 1.0: Increases rotation "swing" for faster animations
			// scale < 1.0: Reduces rotation "swing" for slower animations
			// This modifies the angular velocity in quaternion log space
			void SetVelocityScale(float scale);

			// Set tension parameter (optional fine-tuning)
			// tension = 0.0: Normal Catmull-Rom (default)
			// tension > 0.0: Tighter rotation curves, less swing
			// tension < 0.0: Looser rotation curves, more swing
			// Range typically: -1.0 to 1.0
			void SetTension(float tension);

			float GetVelocityScale() const;
			float GetTension() const;

			// Sample the spline at a given time with current velocity scaling
			math::SimdQuaternion Sample(float time) const;

			// Sample angular velocity (first derivative in quaternion space)
			// Returns the instantaneous axis-angle representation of rotation rate
			// This reflects the current velocity_scale setting
			math::SimdQuaternion SampleAngularVelocity(float time) const;

			Mode GetMode() const;
			size_t GetKeyFrameCount() const;
			const KeyFrame& GetKeyFrame(size_t index) const;

		private:
			std::vector<KeyFrame> keyframes_;
			std::vector<math::SimdQuaternion> base_tangents_;  // Base tangents in log space (unscaled)
			Mode mode_;
			float velocity_scale_;  // Dynamic velocity scaling factor
			float tension_;         // Curve tension parameter

			// Normalize quaternions and ensure shortest path interpolation
			void NormalizeAndFixHandedness();

			// Compute base tangents using Catmull-Rom style approach in quaternion log space
			void ComputeBaseTangents();

			// Find the segment containing the given time
			size_t FindSegment(float time) const;

			// Get scaled tangent for current velocity_scale
			math::SimdQuaternion GetScaledTangent(size_t idx) const;

			// Cubic hermite interpolation for quaternions using squad-like approach
			math::SimdQuaternion CubicInterpolate(size_t idx, float t) const;

			// Derivative of cubic hermite in quaternion space
			math::SimdQuaternion CubicDerivative(size_t idx, float t, float dt) const;

			// Helper functions for quaternion operations in tangent space
			static math::SimdQuaternion Slerp(const math::SimdQuaternion& q0,
				const math::SimdQuaternion& q1,
				float t);

			static math::SimdQuaternion QuaternionLog(const math::SimdQuaternion& q);

			static math::SimdQuaternion QuaternionExp(const math::SimdQuaternion& q);

			static math::SimdQuaternion QuaternionConjugate(const math::SimdQuaternion& q);

			static math::SimdQuaternion QuaternionMultiply(const math::SimdQuaternion& a,
				const math::SimdQuaternion& b);

			static math::SimdQuaternion QuaternionScale(const math::SimdQuaternion& q, float s);

			static math::SimdQuaternion QuaternionAdd(const math::SimdQuaternion& a,
				const math::SimdQuaternion& b);
		};

		// Cubic spline interpolation for SIMD float4 (3D points/vectors)
		// Supports both self-contained and continuous (looping) modes
		// With dynamic velocity scaling for speed-responsive animation curves
		class Float4CubicSpline
		{
		public:
			enum class Mode
			{
				kSelfContained,  // Standard spline, end values determine boundaries
				kContinuous      // Smooth loop from end to start
			};

			// Key frame data structure
			struct KeyFrame
			{
				math::SimdFloat4 value;
				float time;

				KeyFrame();
				KeyFrame(const math::SimdFloat4& v, float t);
			};

			Float4CubicSpline();

			// Initialize with keyframes and mode
			void Initialize(const std::vector<KeyFrame>& keyframes, Mode mode = Mode::kSelfContained);

			// Convenience initialization from separate arrays
			void Initialize(const std::vector<math::SimdFloat4>& values,
				const std::vector<float>& times,
				Mode mode = Mode::kSelfContained);

			// Set velocity scale factor
			// scale > 1.0: Increases curve "swing" for faster animations
			// scale < 1.0: Reduces curve "swing" for slower animations
			// This modifies the tangent magnitudes to create more/less exaggerated arcs
			void SetVelocityScale(float scale);

			// Set tension parameter (optional fine-tuning)
			// tension = 0.0: Normal Catmull-Rom (default)
			// tension > 0.0: Tighter curves, less swing
			// tension < 0.0: Looser curves, more swing (combined with velocity_scale for dramatic effect)
			// Range typically: -1.0 to 1.0
			void SetTension(float tension);

			float GetVelocityScale() const;
			float GetTension() const;

			// Sample the spline at a given time with current velocity scaling
			math::SimdFloat4 Sample(float time) const;

			// Sample velocity (first derivative) at a given time
			// This reflects the current velocity_scale setting
			math::SimdFloat4 SampleVelocity(float time) const;

			Mode GetMode() const;
			size_t GetKeyFrameCount() const;
			const KeyFrame& GetKeyFrame(size_t index) const;

		private:
			std::vector<KeyFrame> keyframes_;
			std::vector<math::SimdFloat4> base_tangents_;  // Base tangents (unscaled)
			Mode mode_;
			float velocity_scale_;  // Dynamic velocity scaling factor
			float tension_;         // Curve tension parameter

			// Compute base tangents using Catmull-Rom approach with tension
			void ComputeBaseTangents();

			// Find the segment containing the given time
			size_t FindSegment(float time) const;

			// Get scaled tangent for current velocity_scale
			math::SimdFloat4 GetScaledTangent(size_t idx) const;

			// Cubic hermite interpolation with dynamically scaled tangents
			math::SimdFloat4 CubicInterpolate(size_t idx, float t) const;

			// Derivative of cubic hermite interpolation with scaled tangents
			math::SimdFloat4 CubicDerivative(size_t idx, float t, float dt) const;
		};

	}  // namespace animation
}  // namespace ozz
