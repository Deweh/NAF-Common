#include "Node.h"

namespace Animation
{
	Node::~Node() {}

	GameNode::GameNode(RE::NiAVObject* n) :
		n(n)
	{
		// Under the hood, NiTransform is a 16-aligned 4x4 float matrix which follows the same conventions as ozz's Float4x4 matrix,
		// therefore we can safely reinterpret cast a NiTransform to a Float4x4.
		localMatrix = reinterpret_cast<ozz::math::Float4x4*>(&n->local);
	}

	Transform GameNode::GetLocal()
	{
		return n->local;
	}

	Transform GameNode::GetWorld()
	{
		return n->world;
	}

	void GameNode::SetLocal(const Transform& t)
	{
		t.ToReal(n->local);
	}

	void GameNode::SetWorld(const Transform& t)
	{
		t.ToReal(n->world);
	}

	void GameNode::SetLocalReal(const RE::NiMatrix3& rot, const RE::NiPoint3& pos)
	{
		n->local.rotate = rot;
		n->local.translate = pos;
	}

	const char* GameNode::GetName()
	{
		return n->name.c_str();
	}

	GameNode::~GameNode() {}

	Transform NullNode::GetLocal() { return Transform(); }
	Transform NullNode::GetWorld() { return Transform(); }
	void NullNode::SetLocal(const Transform&) {}
	void NullNode::SetWorld(const Transform&) {}
	void NullNode::SetLocalReal(const RE::NiMatrix3&, const RE::NiPoint3&) {}
	const char* NullNode::GetName() { return ""; }
	NullNode::~NullNode() {}
}