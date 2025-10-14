#include "CubicSpline.h"

namespace ozz
{
	namespace animation
	{

		// KeyFrame constructors
		QuaternionCubicSpline::KeyFrame::KeyFrame() :
			rotation(math::SimdQuaternion::identity()), time(0.0f) {}

		QuaternionCubicSpline::KeyFrame::KeyFrame(const math::SimdQuaternion& q, float t) :
			rotation(q), time(t) {}

		// QuaternionCubicSpline constructor
		QuaternionCubicSpline::QuaternionCubicSpline() :
			mode_(Mode::kSelfContained),
			velocity_scale_(1.0f),
			tension_(0.0f) {}

		// Initialize with keyframes and mode
		void QuaternionCubicSpline::Initialize(const std::vector<KeyFrame>& keyframes, Mode mode)
		{
			assert(keyframes.size() >= 2 && "Need at least 2 keyframes");

			keyframes_ = keyframes;
			mode_ = mode;

			// Ensure quaternions are normalized and have consistent handedness
			NormalizeAndFixHandedness();

			// Compute base tangents for cubic interpolation
			ComputeBaseTangents();
		}

		// Convenience initialization from separate arrays
		void QuaternionCubicSpline::Initialize(const std::vector<math::SimdQuaternion>& rotations,
			const std::vector<float>& times,
			Mode mode)
		{
			assert(rotations.size() == times.size() && "Arrays must have same size");

			keyframes_.clear();
			keyframes_.reserve(rotations.size());

			for (size_t i = 0; i < rotations.size(); ++i) {
				keyframes_.emplace_back(rotations[i], times[i]);
			}

			mode_ = mode;
			NormalizeAndFixHandedness();
			ComputeBaseTangents();
		}

		void QuaternionCubicSpline::SetVelocityScale(float scale)
		{
			velocity_scale_ = scale;
		}

		void QuaternionCubicSpline::SetTension(float tension)
		{
			tension_ = tension;
			ComputeBaseTangents();
		}

		float QuaternionCubicSpline::GetVelocityScale() const
		{
			return velocity_scale_;
		}

		float QuaternionCubicSpline::GetTension() const
		{
			return tension_;
		}

		// Sample the spline at a given time with current velocity scaling
		math::SimdQuaternion QuaternionCubicSpline::Sample(float time) const
		{
			assert(!keyframes_.empty() && "Spline not initialized");

			if (keyframes_.size() == 1) {
				return keyframes_[0].rotation;
			}

			// Handle time wrapping for continuous mode
			float local_time = time;
			if (mode_ == Mode::kContinuous) {
				float duration = keyframes_.back().time - keyframes_.front().time;
				if (duration > 0.0f) {
					local_time = keyframes_.front().time +
					             fmodf(time - keyframes_.front().time, duration);
					if (local_time < keyframes_.front().time) {
						local_time += duration;
					}
				}
			}

			// Find the segment
			size_t idx = FindSegment(local_time);

			// Clamp at boundaries for self-contained mode
			if (mode_ == Mode::kSelfContained) {
				if (local_time <= keyframes_.front().time) {
					return keyframes_.front().rotation;
				}
				if (local_time >= keyframes_.back().time) {
					return keyframes_.back().rotation;
				}
			}

			// Compute interpolation parameter
			float t0 = keyframes_[idx].time;
			float t1 = keyframes_[idx + 1].time;
			float t = (local_time - t0) / (t1 - t0);

			// Cubic hermite interpolation with scaled tangents
			return CubicInterpolate(idx, t);
		}

		// Sample angular velocity
		math::SimdQuaternion QuaternionCubicSpline::SampleAngularVelocity(float time) const
		{
			assert(!keyframes_.empty() && "Spline not initialized");

			if (keyframes_.size() == 1) {
				return math::SimdQuaternion::identity();
			}

			// Handle time wrapping for continuous mode
			float local_time = time;
			if (mode_ == Mode::kContinuous) {
				float duration = keyframes_.back().time - keyframes_.front().time;
				if (duration > 0.0f) {
					local_time = keyframes_.front().time +
					             fmodf(time - keyframes_.front().time, duration);
					if (local_time < keyframes_.front().time) {
						local_time += duration;
					}
				}
			}

			// Find the segment
			size_t idx = FindSegment(local_time);

			// Clamp at boundaries for self-contained mode
			if (mode_ == Mode::kSelfContained) {
				if (local_time <= keyframes_.front().time) {
					return math::SimdQuaternion::identity();
				}
				if (local_time >= keyframes_.back().time) {
					return math::SimdQuaternion::identity();
				}
			}

			// Compute interpolation parameter
			float t0 = keyframes_[idx].time;
			float t1 = keyframes_[idx + 1].time;
			float dt = t1 - t0;
			float t = (local_time - t0) / dt;

			// Derivative of cubic hermite interpolation
			return CubicDerivative(idx, t, dt);
		}

		QuaternionCubicSpline::Mode QuaternionCubicSpline::GetMode() const
		{
			return mode_;
		}

		size_t QuaternionCubicSpline::GetKeyFrameCount() const
		{
			return keyframes_.size();
		}

		const QuaternionCubicSpline::KeyFrame& QuaternionCubicSpline::GetKeyFrame(size_t index) const
		{
			return keyframes_[index];
		}

		// Private methods

		void QuaternionCubicSpline::NormalizeAndFixHandedness()
		{
			for (size_t i = 0; i < keyframes_.size(); ++i) {
				keyframes_[i].rotation = math::Normalize(keyframes_[i].rotation);

				// Ensure we take shortest path by flipping quaternions if needed
				if (i > 0) {
					math::SimdFloat4 dot = math::Dot4(keyframes_[i - 1].rotation.xyzw,
						keyframes_[i].rotation.xyzw);
					float dot_val;
					math::Store1Ptr(dot, &dot_val);

					if (dot_val < 0.0f) {
						keyframes_[i].rotation.xyzw = math::Xor(
							keyframes_[i].rotation.xyzw,
							math::simd_float4::Load(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF));
					}
				}
			}
		}

		void QuaternionCubicSpline::ComputeBaseTangents()
		{
			size_t n = keyframes_.size();
			base_tangents_.resize(n);

			for (size_t i = 0; i < n; ++i) {
				math::SimdQuaternion q_prev, q_next;

				if (mode_ == Mode::kContinuous) {
					// For continuous mode, wrap around
					q_prev = (i == 0) ? keyframes_[n - 1].rotation : keyframes_[i - 1].rotation;
					q_next = (i == n - 1) ? keyframes_[0].rotation : keyframes_[i + 1].rotation;
				} else {
					// For self-contained, duplicate endpoints
					q_prev = (i == 0) ? keyframes_[0].rotation : keyframes_[i - 1].rotation;
					q_next = (i == n - 1) ? keyframes_[n - 1].rotation : keyframes_[i + 1].rotation;
				}

				// Compute tangent as average of adjacent segments in log space
				math::SimdQuaternion q_curr = keyframes_[i].rotation;
				math::SimdQuaternion delta_prev = QuaternionLog(QuaternionMultiply(
					QuaternionConjugate(q_prev), q_curr));
				math::SimdQuaternion delta_next = QuaternionLog(QuaternionMultiply(
					QuaternionConjugate(q_curr), q_next));

				// Average the log-space deltas and scale by 0.5 for tangent
				// Apply tension: reduces tangent magnitude for tighter curves
				float tangent_scale = 0.25f * (1.0f - tension_);
				base_tangents_[i] = QuaternionScale(
					QuaternionAdd(delta_prev, delta_next), tangent_scale);
			}
		}

		size_t QuaternionCubicSpline::FindSegment(float time) const
		{
			// Linear search - binary search would be more efficient for many keyframes
			for (size_t i = 0; i < keyframes_.size() - 1; ++i) {
				if (time >= keyframes_[i].time && time <= keyframes_[i + 1].time) {
					return i;
				}
			}
			return keyframes_.size() - 2;
		}

		math::SimdQuaternion QuaternionCubicSpline::GetScaledTangent(size_t idx) const
		{
			return QuaternionScale(base_tangents_[idx], velocity_scale_);
		}

		math::SimdQuaternion QuaternionCubicSpline::CubicInterpolate(size_t idx, float t) const
		{
			const math::SimdQuaternion& q0 = keyframes_[idx].rotation;
			const math::SimdQuaternion& q1 = keyframes_[idx + 1].rotation;

			// Get scaled tangents based on current velocity_scale
			math::SimdQuaternion tan0 = GetScaledTangent(idx);
			math::SimdQuaternion tan1 = GetScaledTangent(idx + 1);

			// Compute control points for squad interpolation
			math::SimdQuaternion a = QuaternionMultiply(q0, QuaternionExp(tan0));
			math::SimdQuaternion b = QuaternionMultiply(q1, QuaternionExp(
																QuaternionScale(tan1, -1.0f)));

			// Squad interpolation: slerp(slerp(q0, q1, t), slerp(a, b, t), 2t(1-t))
			math::SimdQuaternion slerp1 = Slerp(q0, q1, t);
			math::SimdQuaternion slerp2 = Slerp(a, b, t);
			float blend = 2.0f * t * (1.0f - t);

			return Slerp(slerp1, slerp2, blend);
		}

		math::SimdQuaternion QuaternionCubicSpline::CubicDerivative(size_t idx, float t, float dt) const
		{
			const math::SimdQuaternion& q0 = keyframes_[idx].rotation;
			const math::SimdQuaternion& q1 = keyframes_[idx + 1].rotation;

			// Get scaled tangents based on current velocity_scale
			math::SimdQuaternion tan0 = GetScaledTangent(idx);
			math::SimdQuaternion tan1 = GetScaledTangent(idx + 1);

			// Compute control points
			math::SimdQuaternion a = QuaternionMultiply(q0, QuaternionExp(tan0));
			math::SimdQuaternion b = QuaternionMultiply(q1, QuaternionExp(
																QuaternionScale(tan1, -1.0f)));

			// For derivative, we need to compute the rate of change
			// This is an approximation using finite differences in log space
			float epsilon = 0.001f / dt;  // Small step relative to segment
			float t_plus = t + epsilon;
			float t_minus = t - epsilon;

			// Clamp to valid range
			t_plus = (t_plus > 1.0f) ? 1.0f : t_plus;
			t_minus = (t_minus < 0.0f) ? 0.0f : t_minus;

			// Sample at t+epsilon and t-epsilon
			math::SimdQuaternion slerp1_plus = Slerp(q0, q1, t_plus);
			math::SimdQuaternion slerp2_plus = Slerp(a, b, t_plus);
			float blend_plus = 2.0f * t_plus * (1.0f - t_plus);
			math::SimdQuaternion q_plus = Slerp(slerp1_plus, slerp2_plus, blend_plus);

			math::SimdQuaternion slerp1_minus = Slerp(q0, q1, t_minus);
			math::SimdQuaternion slerp2_minus = Slerp(a, b, t_minus);
			float blend_minus = 2.0f * t_minus * (1.0f - t_minus);
			math::SimdQuaternion q_minus = Slerp(slerp1_minus, slerp2_minus, blend_minus);

			// Compute angular velocity in log space
			math::SimdQuaternion delta = QuaternionLog(QuaternionMultiply(
				QuaternionConjugate(q_minus), q_plus));

			// Scale by inverse of time step
			float scale = 1.0f / (2.0f * epsilon);
			return QuaternionScale(delta, scale);
		}

		// Static helper functions

		math::SimdQuaternion QuaternionCubicSpline::Slerp(const math::SimdQuaternion& q0,
			const math::SimdQuaternion& q1,
			float t)
		{
			math::SimdFloat4 dot_result = math::Dot4(q0.xyzw, q1.xyzw);
			float dot;
			math::Store1Ptr(dot_result, &dot);

			const float threshold = 0.9995f;
			if (dot > threshold) {
				// Linear interpolation for close quaternions
				return math::Normalize(math::SimdQuaternion(
					math::NMAdd(q0.xyzw, math::simd_float4::Load1(1.0f - t),
						q1.xyzw * math::simd_float4::Load1(t))));
			}

			dot = math::Clamp(-1.0f, dot, 1.0f);
			float theta = acosf(dot) * t;

			math::SimdQuaternion q2 = math::Normalize(math::SimdQuaternion(
				math::NMAdd(q0.xyzw, math::simd_float4::Load1(dot), q1.xyzw)));

			float cos_theta = cosf(theta);
			float sin_theta = sinf(theta);

			return math::SimdQuaternion(
				q0.xyzw * math::simd_float4::Load1(cos_theta) +
				q2.xyzw * math::simd_float4::Load1(sin_theta));
		}

		math::SimdQuaternion QuaternionCubicSpline::QuaternionLog(const math::SimdQuaternion& q)
		{
			float w;
			math::Store1Ptr(math::SplatW(q.xyzw), &w);

			math::SimdFloat4 vec = math::SetW(q.xyzw, math::simd_float4::zero());
			math::SimdFloat4 len_sq = math::Dot3(vec, vec);
			float length_sq;
			math::Store1Ptr(len_sq, &length_sq);

			if (length_sq < 1e-6f) {
				return math::SimdQuaternion::identity();
			}

			float length = sqrtf(length_sq);
			float angle = atan2f(length, w);
			float scale = angle / length;

			return math::SimdQuaternion(
				math::SetW(vec * math::simd_float4::Load1(scale),
					math::simd_float4::zero()));
		}

		math::SimdQuaternion QuaternionCubicSpline::QuaternionExp(const math::SimdQuaternion& q)
		{
			math::SimdFloat4 vec = math::SetW(q.xyzw, math::simd_float4::zero());
			math::SimdFloat4 len_sq = math::Dot3(vec, vec);
			float length_sq;
			math::Store1Ptr(len_sq, &length_sq);

			if (length_sq < 1e-6f) {
				return math::SimdQuaternion::identity();
			}

			float length = sqrtf(length_sq);
			float sin_angle = sinf(length);
			float cos_angle = cosf(length);
			float scale = sin_angle / length;

			return math::SimdQuaternion(
				math::SetW(vec * math::simd_float4::Load1(scale),
					math::simd_float4::Load1(cos_angle)));
		}

		math::SimdQuaternion QuaternionCubicSpline::QuaternionConjugate(const math::SimdQuaternion& q)
		{
			return math::Conjugate(q);
		}

		math::SimdQuaternion QuaternionCubicSpline::QuaternionMultiply(const math::SimdQuaternion& a,
			const math::SimdQuaternion& b)
		{
			return a * b;
		}

		math::SimdQuaternion QuaternionCubicSpline::QuaternionScale(const math::SimdQuaternion& q, float s)
		{
			return math::SimdQuaternion(q.xyzw * math::simd_float4::Load1(s));
		}

		math::SimdQuaternion QuaternionCubicSpline::QuaternionAdd(const math::SimdQuaternion& a,
			const math::SimdQuaternion& b)
		{
			return math::SimdQuaternion(a.xyzw + b.xyzw);
		}

		// KeyFrame constructors
		Float4CubicSpline::KeyFrame::KeyFrame() :
			value(math::simd_float4::zero()), time(0.0f) {}

		Float4CubicSpline::KeyFrame::KeyFrame(const math::SimdFloat4& v, float t) :
			value(v), time(t) {}

		// Float4CubicSpline constructor
		Float4CubicSpline::Float4CubicSpline() :
			mode_(Mode::kSelfContained),
			velocity_scale_(1.0f),
			tension_(0.0f) {}

		// Initialize with keyframes and mode
		void Float4CubicSpline::Initialize(const std::vector<KeyFrame>& keyframes, Mode mode)
		{
			assert(keyframes.size() >= 2 && "Need at least 2 keyframes");

			keyframes_ = keyframes;
			mode_ = mode;

			// Compute tangents for cubic interpolation
			ComputeBaseTangents();
		}

		// Convenience initialization from separate arrays
		void Float4CubicSpline::Initialize(const std::vector<math::SimdFloat4>& values,
			const std::vector<float>& times,
			Mode mode)
		{
			assert(values.size() == times.size() && "Arrays must have same size");

			keyframes_.clear();
			keyframes_.reserve(values.size());

			for (size_t i = 0; i < values.size(); ++i) {
				keyframes_.emplace_back(values[i], times[i]);
			}

			mode_ = mode;
			ComputeBaseTangents();
		}

		void Float4CubicSpline::SetVelocityScale(float scale)
		{
			velocity_scale_ = scale;
		}

		void Float4CubicSpline::SetTension(float tension)
		{
			tension_ = tension;
			ComputeBaseTangents();
		}

		float Float4CubicSpline::GetVelocityScale() const
		{
			return velocity_scale_;
		}

		float Float4CubicSpline::GetTension() const
		{
			return tension_;
		}

		// Sample the spline at a given time with current velocity scaling
		math::SimdFloat4 Float4CubicSpline::Sample(float time) const
		{
			assert(!keyframes_.empty() && "Spline not initialized");

			if (keyframes_.size() == 1) {
				return keyframes_[0].value;
			}

			// Handle time wrapping for continuous mode
			float local_time = time;
			if (mode_ == Mode::kContinuous) {
				float duration = keyframes_.back().time - keyframes_.front().time;
				if (duration > 0.0f) {
					local_time = keyframes_.front().time +
					             fmodf(time - keyframes_.front().time, duration);
					if (local_time < keyframes_.front().time) {
						local_time += duration;
					}
				}
			}

			// Find the segment
			size_t idx = FindSegment(local_time);

			// Clamp at boundaries for self-contained mode
			if (mode_ == Mode::kSelfContained) {
				if (local_time <= keyframes_.front().time) {
					return keyframes_.front().value;
				}
				if (local_time >= keyframes_.back().time) {
					return keyframes_.back().value;
				}
			}

			// Compute interpolation parameter
			float t0 = keyframes_[idx].time;
			float t1 = keyframes_[idx + 1].time;
			float t = (local_time - t0) / (t1 - t0);

			// Cubic hermite interpolation
			return CubicInterpolate(idx, t);
		}

		// Sample velocity (first derivative) at a given time
		math::SimdFloat4 Float4CubicSpline::SampleVelocity(float time) const
		{
			assert(!keyframes_.empty() && "Spline not initialized");

			if (keyframes_.size() == 1) {
				return math::simd_float4::zero();
			}

			// Handle time wrapping for continuous mode
			float local_time = time;
			if (mode_ == Mode::kContinuous) {
				float duration = keyframes_.back().time - keyframes_.front().time;
				if (duration > 0.0f) {
					local_time = keyframes_.front().time +
					             fmodf(time - keyframes_.front().time, duration);
					if (local_time < keyframes_.front().time) {
						local_time += duration;
					}
				}
			}

			// Find the segment
			size_t idx = FindSegment(local_time);

			// Clamp at boundaries for self-contained mode
			if (mode_ == Mode::kSelfContained) {
				if (local_time <= keyframes_.front().time) {
					return math::simd_float4::zero();
				}
				if (local_time >= keyframes_.back().time) {
					return math::simd_float4::zero();
				}
			}

			// Compute interpolation parameter
			float t0 = keyframes_[idx].time;
			float t1 = keyframes_[idx + 1].time;
			float dt = t1 - t0;
			float t = (local_time - t0) / dt;

			// Derivative of cubic hermite interpolation
			return CubicDerivative(idx, t, dt);
		}

		Float4CubicSpline::Mode Float4CubicSpline::GetMode() const
		{
			return mode_;
		}

		size_t Float4CubicSpline::GetKeyFrameCount() const
		{
			return keyframes_.size();
		}

		const Float4CubicSpline::KeyFrame& Float4CubicSpline::GetKeyFrame(size_t index) const
		{
			return keyframes_[index];
		}

		// Private methods

		void Float4CubicSpline::ComputeBaseTangents()
		{
			size_t n = keyframes_.size();
			base_tangents_.resize(n);

			for (size_t i = 0; i < n; ++i) {
				math::SimdFloat4 p_prev, p_next;
				float dt_prev = 1.0f, dt_next = 1.0f;

				if (mode_ == Mode::kContinuous) {
					// For continuous mode, wrap around
					if (i == 0) {
						p_prev = keyframes_[n - 1].value;
						dt_prev = keyframes_[0].time - keyframes_[n - 1].time +
						          (keyframes_[n - 1].time - keyframes_[0].time);
					} else {
						p_prev = keyframes_[i - 1].value;
						dt_prev = keyframes_[i].time - keyframes_[i - 1].time;
					}

					if (i == n - 1) {
						p_next = keyframes_[0].value;
						dt_next = keyframes_[n - 1].time - keyframes_[0].time +
						          (keyframes_[1].time - keyframes_[0].time);
					} else {
						p_next = keyframes_[i + 1].value;
						dt_next = keyframes_[i + 1].time - keyframes_[i].time;
					}
				} else {
					// For self-contained, use one-sided differences at endpoints
					if (i == 0) {
						p_prev = keyframes_[0].value;
						dt_prev = keyframes_[1].time - keyframes_[0].time;
					} else {
						p_prev = keyframes_[i - 1].value;
						dt_prev = keyframes_[i].time - keyframes_[i - 1].time;
					}

					if (i == n - 1) {
						p_next = keyframes_[n - 1].value;
						dt_next = keyframes_[n - 1].time - keyframes_[n - 2].time;
					} else {
						p_next = keyframes_[i + 1].value;
						dt_next = keyframes_[i + 1].time - keyframes_[i].time;
					}
				}

				math::SimdFloat4 p_curr = keyframes_[i].value;

				// Catmull-Rom tangent: weighted average of adjacent slopes
				// With non-uniform parameterization for better results

				if (i == 0 && mode_ == Mode::kSelfContained) {
					// At start for self-contained: use forward difference
					base_tangents_[i] = (p_next - p_curr) * math::simd_float4::Load1((1.0f - tension_) / dt_next);
				} else if (i == n - 1 && mode_ == Mode::kSelfContained) {
					// At end for self-contained: use backward difference
					base_tangents_[i] = (p_curr - p_prev) * math::simd_float4::Load1((1.0f - tension_) / dt_prev);
				} else {
					// Interior point or continuous mode: centered difference with tension
					float alpha = dt_prev / (dt_prev + dt_next);

					math::SimdFloat4 slope_prev = (p_curr - p_prev) * math::simd_float4::Load1(1.0f / dt_prev);
					math::SimdFloat4 slope_next = (p_next - p_curr) * math::simd_float4::Load1(1.0f / dt_next);

					math::SimdFloat4 tangent = slope_prev * math::simd_float4::Load1(1.0f - alpha) +
					                           slope_next * math::simd_float4::Load1(alpha);

					// Apply tension
					base_tangents_[i] = tangent * math::simd_float4::Load1(1.0f - tension_);
				}
			}
		}

		size_t Float4CubicSpline::FindSegment(float time) const
		{
			// Linear search - binary search would be more efficient for many keyframes
			for (size_t i = 0; i < keyframes_.size() - 1; ++i) {
				if (time >= keyframes_[i].time && time <= keyframes_[i + 1].time) {
					return i;
				}
			}
			return keyframes_.size() - 2;
		}

		math::SimdFloat4 Float4CubicSpline::GetScaledTangent(size_t idx) const
		{
			return base_tangents_[idx] * math::simd_float4::Load1(velocity_scale_);
		}

		math::SimdFloat4 Float4CubicSpline::CubicInterpolate(size_t idx, float t) const
		{
			const math::SimdFloat4& p0 = keyframes_[idx].value;
			const math::SimdFloat4& p1 = keyframes_[idx + 1].value;

			// Get scaled tangents based on current velocity_scale
			math::SimdFloat4 m0 = GetScaledTangent(idx);
			math::SimdFloat4 m1 = GetScaledTangent(idx + 1);

			float dt = keyframes_[idx + 1].time - keyframes_[idx].time;

			// Hermite basis functions
			float t2 = t * t;
			float t3 = t2 * t;

			float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;  // (2t³ - 3t² + 1)
			float h10 = t3 - 2.0f * t2 + t;            // (t³ - 2t² + t)
			float h01 = -2.0f * t3 + 3.0f * t2;        // (-2t³ + 3t²)
			float h11 = t3 - t2;                       // (t³ - t²)

			// p(t) = h00*p0 + h10*dt*m0 + h01*p1 + h11*dt*m1
			math::SimdFloat4 result = p0 * math::simd_float4::Load1(h00) +
			                          m0 * math::simd_float4::Load1(h10 * dt) +
			                          p1 * math::simd_float4::Load1(h01) +
			                          m1 * math::simd_float4::Load1(h11 * dt);

			return result;
		}

		math::SimdFloat4 Float4CubicSpline::CubicDerivative(size_t idx, float t, float dt) const
		{
			const math::SimdFloat4& p0 = keyframes_[idx].value;
			const math::SimdFloat4& p1 = keyframes_[idx + 1].value;

			// Get scaled tangents based on current velocity_scale
			math::SimdFloat4 m0 = GetScaledTangent(idx);
			math::SimdFloat4 m1 = GetScaledTangent(idx + 1);

			// Derivative of hermite basis functions
			float t2 = t * t;

			float dh00 = 6.0f * t2 - 6.0f * t;         // d/dt(2t³ - 3t² + 1)
			float dh10 = 3.0f * t2 - 4.0f * t + 1.0f;  // d/dt(t³ - 2t² + t)
			float dh01 = -6.0f * t2 + 6.0f * t;        // d/dt(-2t³ + 3t²)
			float dh11 = 3.0f * t2 - 2.0f * t;         // d/dt(t³ - t²)

			// dp/dt = (dh00*p0 + dh10*dt*m0 + dh01*p1 + dh11*dt*m1) / dt
			math::SimdFloat4 derivative = p0 * math::simd_float4::Load1(dh00 / dt) +
			                              m0 * math::simd_float4::Load1(dh10) +
			                              p1 * math::simd_float4::Load1(dh01 / dt) +
			                              m1 * math::simd_float4::Load1(dh11);

			return derivative;
		}


	}  // namespace animation
}  // namespace ozz