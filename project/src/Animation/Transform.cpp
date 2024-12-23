#include "Transform.h"
#include "ozz/base/maths/vec_float.h"

namespace Animation
{
	void XYZTransform::ConstrainRotationAngles()
	{
		rotate.x = std::fmodf(rotate.x, 2.0f * M_PI);
		rotate.y = std::fmodf(rotate.y, 2.0f * M_PI);
		rotate.z = std::fmodf(rotate.z, 2.0f * M_PI);
	}

	Transform::Transform() {}

	Transform::Transform(const RE::NiQuaternion& rotate, const RE::NiPoint3& translate) :
		rotate(rotate), translate(translate) {}

	Transform::Transform(const RE::NiTransform& t)
	{
		FromReal(t);
	}

	Transform::Transform(const ozz::math::Transform& t)
	{
		FromOzz(t);
	}

	void Transform::FromOzz(const ozz::math::Transform& t)
	{
		rotate.x = t.rotation.x;
		rotate.y = t.rotation.y;
		rotate.z = t.rotation.z;
		rotate.w = t.rotation.w;
		translate.x = t.translation.x;
		translate.y = t.translation.y;
		translate.z = t.translation.z;
	}

	void Transform::ExtractSoaTransform(const ozz::math::SoaTransform& in, std::array<Transform, 4>& out)
	{
		alignas(16) float components[8][4];
		_mm_store_ps(components[0], in.translation.x);
		_mm_store_ps(components[1], in.translation.y);
		_mm_store_ps(components[2], in.translation.z);

		_mm_store_ps(components[3], in.rotation.w);
		_mm_store_ps(components[4], in.rotation.x);
		_mm_store_ps(components[5], in.rotation.y);
		_mm_store_ps(components[6], in.rotation.z);

		for (int i = 0; i < 4; i++) {
			out[i].translate.x = components[0][i];
			out[i].translate.y = components[1][i];
			out[i].translate.z = components[2][i];

			out[i].rotate.w = components[3][i];
			out[i].rotate.x = components[4][i];
			out[i].rotate.y = components[5][i];
			out[i].rotate.z = components[6][i];
		}
	}

	void Transform::StoreSoaTransform(const std::span<Transform, 4>& in, ozz::math::SoaTransform& out)
	{
#define SOA_SET(smblOut, smblIn, prt) out.smblOut.prt = _mm_set_ps(in[3].smblIn.prt, in[2].smblIn.prt, in[1].smblIn.prt, in[0].smblIn.prt)

		SOA_SET(translation, translate, x);
		SOA_SET(translation, translate, y);
		SOA_SET(translation, translate, z);

		SOA_SET(rotation, rotate, w);
		SOA_SET(rotation, rotate, x);
		SOA_SET(rotation, rotate, y);
		SOA_SET(rotation, rotate, z);

#undef SOA_SET
	}

	void Transform::ExtractSoaTransforms(const std::span<const ozz::math::SoaTransform>& in, const std::function<void(size_t, const Transform&)> func)
	{
		size_t s = in.size();
		std::array<Transform, 4> arr;

		for (size_t i = 0; i < s; i++) {
			size_t j = i * 4;
			ExtractSoaTransform(in[i], arr);
			func(j, arr[0]);
			func(j + 1, arr[1]);
			func(j + 2, arr[2]);
			func(j + 3, arr[3]);
		}
	}

	void Transform::StoreSoaTransforms(std::vector<ozz::math::SoaTransform>& out, const std::function<Transform(size_t)> func)
	{
		size_t s = out.size();
		std::array<Transform, 4> arr;

		for (size_t i = 0; i < s; i++) {
			size_t j = i * 4;
			arr[0] = func(j);
			arr[1] = func(j + 1);
			arr[2] = func(j + 2);
			arr[3] = func(j + 3);
			StoreSoaTransform(arr, out[i]);
		}
	}

	void Transform::StoreSoaTransforms(const std::span<ozz::math::SoaTransform>& out, const std::function<Transform(size_t)> func)
	{
		size_t s = out.size();
		std::array<Transform, 4> arr;

		for (size_t i = 0; i < s; i++) {
			size_t j = i * 4;
			arr[0] = func(j);
			arr[1] = func(j + 1);
			arr[2] = func(j + 2);
			arr[3] = func(j + 3);
			StoreSoaTransform(arr, out[i]);
		}
	}

	void Transform::ExtractSoaMatrixPoint(const ozz::math::SoaFloat4x4& mat_in, const ozz::math::SoaFloat3& pt_in, std::array<RE::NiMatrix3, 4>& mat_out, std::array<RE::NiPoint3, 4>& pt_out)
	{
		// Extract point
		_mm_store_ps(reinterpret_cast<float*>(&pt_out[0].x), pt_in.x);
		_mm_store_ps(reinterpret_cast<float*>(&pt_out[0].y), pt_in.y);
		_mm_store_ps(reinterpret_cast<float*>(&pt_out[0].z), pt_in.z);

		// Extract matrix
		for (int i = 0; i < 3; ++i) {
			__m128 row0 = _mm_load_ps(&(reinterpret_cast<const float*>(&mat_in.cols[0]))[i]);
			__m128 row1 = _mm_load_ps(&(reinterpret_cast<const float*>(&mat_in.cols[1]))[i]);
			__m128 row2 = _mm_load_ps(&(reinterpret_cast<const float*>(&mat_in.cols[2]))[i]);
			__m128 row3 = _mm_load_ps(&(reinterpret_cast<const float*>(&mat_in.cols[3]))[i]);

			// Transpose 4x4 matrix
			_MM_TRANSPOSE4_PS(row0, row1, row2, row3);

			// Store transposed rows
			_mm_store_ps(reinterpret_cast<float*>(&mat_out[0].entry[i][0]), row0);
			_mm_store_ps(reinterpret_cast<float*>(&mat_out[1].entry[i][0]), row1);
			_mm_store_ps(reinterpret_cast<float*>(&mat_out[2].entry[i][0]), row2);
			_mm_store_ps(reinterpret_cast<float*>(&mat_out[3].entry[i][0]), row3);
		}
	}

	void Transform::ExtractSoaTransformsReal(const std::span<ozz::math::SoaTransform>& in, const extract_func func, void* userPtr)
	{
		size_t s = in.size();
		ozz::math::SoaFloat4x4 mat;
		std::array<RE::NiPoint3, 4> pos;
		std::array<RE::NiMatrix3, 4> rot;

		for (size_t i = 0; i < s; i++) {
			auto& cur = in[i];
			mat = ozz::math::SoaFloat4x4::FromQuaternion(cur.rotation);
			ExtractSoaMatrixPoint(mat, cur.translation, rot, pos);
			size_t j = i * 4;
			func(j, rot[0], pos[0], userPtr);
			func(j + 1, rot[1], pos[1], userPtr);
			func(j + 2, rot[2], pos[2], userPtr);
			func(j + 3, rot[3], pos[3], userPtr);
		}
	}

	void Transform::ExtractSoaTransformsReal(const std::span<ozz::math::SoaTransform>& in, std::span<RE::NiTransform>& out)
	{
		if (in.size() < 2 || out.size() < 2)
			return;

		auto itIn = in.begin();
		auto itOut = out.begin();

		for (; itIn != in.end() && itOut != out.end(); ++itIn, itOut += 4) {
			const ozz::math::SoaFloat4x4 soa_mat = ozz::math::SoaFloat4x4::FromAffine(itIn->translation, itIn->rotation, itIn->scale);
			ozz::math::Float4x4 aos_mats[4];
			ozz::math::Transpose16x16(&soa_mat.cols[0].x, aos_mats->cols);
		}
	}

	void Transform::ToReal(RE::NiTransform& t) const
	{
		rotate.ToMatrix(t.rotate);
		t.translate = translate;
	}

	void Transform::FromReal(const RE::NiTransform& t)
	{
		rotate.FromMatrix(t.rotate);
		translate = t.translate;
	}

	bool Transform::IsIdentity() const
	{
		return (
			std::fabs(translate.x) < 0.0001f &&
			std::fabs(translate.y) < 0.0001f &&
			std::fabs(translate.z) < 0.0001f &&
			std::fabs(rotate.w - 1.0f) < 0.0001f &&
			std::fabs(rotate.x) < 0.0001f &&
			std::fabs(rotate.y) < 0.0001f &&
			std::fabs(rotate.z) < 0.0001f);
	}

	void Transform::MakeIdentity()
	{
		translate.x = 0.0f;
		translate.y = 0.0f;
		translate.z = 0.0f;
		rotate.w = 1.0f;
		rotate.x = 0.0f;
		rotate.y = 0.0f;
		rotate.z = 0.0f;
	}

	Transform Transform::operator-(const Transform& rhs) const
	{
		Transform result;
		result.rotate = rotate * rhs.rotate.InvertVector();
		result.translate = translate - rhs.translate;
		return result;
	}

	Transform Transform::operator*(const Transform& rhs) const
	{
		Transform result;
		result.rotate = rotate * rhs.rotate;
		result.translate = translate + rhs.translate;
		return result;
	}
}