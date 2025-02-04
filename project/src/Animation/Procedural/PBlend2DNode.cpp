#include "PBlend2DNode.h"
#include "delaunator-header-only.hpp"

namespace Animation::Procedural
{
	namespace detail
	{
		bool CalculateBarycentricCoords(const ozz::math::Float2& a_pt, const PBlend2DNode::BlendTriangle& a_tri, float& w1, float& w2, float& w3)
		{
			float x1 = a_tri.a.x, y1 = a_tri.a.y;
			float x2 = a_tri.b.x, y2 = a_tri.b.y;
			float x3 = a_tri.c.x, y3 = a_tri.c.y;
			float x = a_pt.x, y = a_pt.y;

			float denom = (y2 - y3) * (x1 - x3) + (x3 - x2) * (y1 - y3);
			if (denom < 0.0000001f) return false; // Degenerate triangle

			w1 = ((y2 - y3) * (x - x3) + (x3 - x2) * (y - y3)) / denom;
			w2 = ((y3 - y1) * (x - x3) + (x1 - x3) * (y - y3)) / denom;
			w3 = 1.0f - w1 - w2;

			return (w1 >= 0.0f && w2 >= 0.0f && w3 >= 0.0f); // Point is inside the triangle
		}

		size_t GetMinimum(float a, float b, float c) {
			return (a < b) ? ((a < c) ? 0 : 2) : ((b < c) ? 1 : 2);
		}
	}

	std::unique_ptr<PNodeInstanceData> PBlend2DNode::CreateInstanceData()
	{
		std::unique_ptr<InstanceData> result = std::make_unique<InstanceData>();
		result->lastTri = triangles.begin();
		return result;
	}

	PEvaluationResult PBlend2DNode::Evaluate(PNodeInstanceData* a_instanceData, PoseCache& a_poseCache, PEvaluationContext& a_evalContext)
	{
		ozz::math::Float2 pt = { GetRequiredInput<float>(0, a_evalContext), GetRequiredInput<float>(1, a_evalContext) };

		auto inst = static_cast<InstanceData*>(a_instanceData);
		PoseCache::Handle output = a_poseCache.acquire_handle();

		constexpr uint8_t maxIterations = 100;
		uint8_t curIteration = 0;
		tri_vector_t::iterator curTri = inst->lastTri;

		bool outsideHull = false;
		float w1, w2, w3;

		// To find the triangle that the current blend point is inside,
		// walk along the convex hull in the direction of the point
		// until a triangle is found which the point is inside, or the
		// edge of the convex hull is reached.
		while(curIteration < maxIterations) {
			if(detail::CalculateBarycentricCoords(pt, *curTri, w1, w2, w3)) {
				break;
			}

			const size_t maxViolation = detail::GetMinimum(w1, w2, w3);
			const size_t nextTri = curTri->walkAdjacencies[maxViolation];

			if(nextTri == BlendTriangle::NO_ADJACENT_TRI) {
				outsideHull = true;
				break;
			}
			
			curTri = triangles.begin() + nextTri;
			curIteration += 1;
		}

		// If the point is outside the convex hull, clamp
		// weights to a minimum of 0, then normalize them
		// so that they add up to 1.0
		if(outsideHull) {
			// TODO: If the point is far outside the convex hull,
			// then a standard barycentric-based walk may not find the nearest triangle.
			// In that case, an extra distance-based step would be required to find the
			// nearest triangle to retain smooth blending behavior.

			w1 = std::max(0.0f, w1);
			w2 = std::max(0.0f, w2);
			w3 = std::max(0.0f, w3);

			float normalFactor = 1.0f / (w1 + w2 + w3);
			w1 *= normalFactor;
			w2 *= normalFactor;
			w3 *= normalFactor;
		}

		inst->lastTri = curTri;

		std::array<ozz::animation::BlendingJob::Layer, 3> blendLayers;
		blendLayers[0].transform = GetVariadicInput<PoseCache::Handle>(curTri->inputA, a_evalContext).get_ozz();
		blendLayers[0].weight = w1;
		blendLayers[1].transform = GetVariadicInput<PoseCache::Handle>(curTri->inputB, a_evalContext).get_ozz();
		blendLayers[1].weight = w2;
		blendLayers[2].transform = GetVariadicInput<PoseCache::Handle>(curTri->inputC, a_evalContext).get_ozz();
		blendLayers[2].weight = w3;

		ozz::animation::BlendingJob blendJob;
		blendJob.output = output.get_ozz();
		blendJob.rest_pose = blendLayers[0].transform;
		blendJob.layers = ozz::make_span(blendLayers);
		blendJob.threshold = 1.0f;
		blendJob.Run();

		return output;
	}

	bool PBlend2DNode::SetCustomValues(const std::span<PEvaluationResult>& a_values, const OzzSkeleton* a_skeleton, const std::filesystem::path& a_localDir)
	{
		std::vector<ozz::math::Float2> blendPoints;

		// TODO: Fill in blendPoints based on the blend point for each input pose.

		// We need at least three points to form a blend triangle.
		// If there's only 2 points, then a 1D blend space should be used instead.
		if(blendPoints.size() < 3) {
			return false;
		}

		std::vector<double> tempPoints;
		tempPoints.reserve(blendPoints.size() * 2);
		for(const auto& pt : blendPoints) {
			tempPoints.push_back(pt.x);
			tempPoints.push_back(pt.y);
		}

		delaunator::Delaunator d(tempPoints);

		const auto GetAdjacentTri = [&d](size_t a_edgeIdx) -> size_t{
			size_t adjacentEdge = d.halfedges[a_edgeIdx];
			if(adjacentEdge == delaunator::INVALID_INDEX) {
				return BlendTriangle::NO_ADJACENT_TRI;
			} else {
				return adjacentEdge / 3;
			}
		};

		triangles.reserve(d.triangles.size() / 3);
		for(size_t i = 0; i < d.triangles.size(); i += 3) {
			BlendTriangle& tri = triangles.emplace_back();
			size_t idxA = d.triangles[i];
			size_t idxB = d.triangles[i + 1];
			size_t idxC = d.triangles[i + 2];

			tri.a = blendPoints[idxA];
			tri.b = blendPoints[idxB];
			tri.c = blendPoints[idxC];

			tri.inputA = idxA;
			tri.inputB = idxB;
			tri.inputC = idxC;

			// If a point's barycentric coordinate is negative,
			// then we should walk towards the edge opposite of it, like so:
			// A => BC
			// B => CA
			// C => AB 
			tri.walkAdjacencies[0] = GetAdjacentTri(i + 1);
			tri.walkAdjacencies[1] = GetAdjacentTri(i + 2);
			tri.walkAdjacencies[2] = GetAdjacentTri(i);
		}

		return true;
	}
}