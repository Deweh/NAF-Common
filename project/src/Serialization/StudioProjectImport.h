#pragma once

#include "Animation/Studio/ProjectEx.h"

namespace Serialization
{
	// Loading takes two steps. Read() parses and validates the file without changing anything in the game, then the user binds
	// an actor to each actor slot, and Build() creates the project. Skeletons come from the bound actors, since
	// they aren't known until an actor using them has been animated.
	class StudioProjectImport
	{
	public:
		using ProjectEx = Animation::Studio::ProjectEx;

		StudioProjectImport() = delete;

		struct ActorSlot
		{
			ProjectEx::ObjectId id{ ProjectEx::kNoObject };
			std::string name;
			std::string skeletonId;
			RE::SEX gender{ RE::SEX::kNone };
			RE::TESFormID bindingHint{ 0 };  // The actor last bound to the slot, if its form still resolves. Zero otherwise.
			std::string bindingHintName;     // Its name when saved, for display.
		};

		struct ProjectFile;  // Parsed contents, defined in the .cpp.
		struct RigFile;
		struct FileObject;

		class PendingProject
		{
		public:
			[[nodiscard]] std::span<const ActorSlot> GetActorSlots() const;
			// The UI's "editor" section as JSON text. Empty when the file has none.
			[[nodiscard]] const std::string& GetEditorJson() const;

		private:
			friend class StudioProjectImport;
			std::shared_ptr<const ProjectFile> m_file;
		};

		struct Result
		{
			std::unique_ptr<ProjectEx> project;
			std::vector<std::string> warnings;  // Non-fatal problems, such as tracks dropped because their bone no longer exists.
		};

		static std::expected<PendingProject, std::string> Read(const std::filesystem::path& a_path);
		// a_actors holds one actor per slot, in slot order. Each must satisfy its slot's requirements.
		static std::expected<Result, std::string> Build(const PendingProject& a_pending, std::span<RE::Actor* const> a_actors);

		// ---- Rigs ----

		struct RigSummary
		{
			size_t ikChains{ 0 };
			size_t hands{ 0 };
			size_t splineIKs{ 0 };
			size_t posedBones{ 0 };
			size_t maskOverrides{ 0 };
		};

		class PendingRig
		{
		public:
			[[nodiscard]] const std::string& GetSkeletonId() const;
			[[nodiscard]] RE::SEX GetGender() const;
			[[nodiscard]] const RigSummary& GetSummary() const;
			[[nodiscard]] const std::string& GetEditorJson() const;

		private:
			friend class StudioProjectImport;
			std::shared_ptr<const RigFile> m_file;
		};

		// Parses and validates a rig file without changing anything.
		static std::expected<PendingRig, std::string> ReadRig(const std::filesystem::path& a_path);
		// One undo step. Fails without changing anything when the actor isn't bound or uses another skeleton. The pose
		// is applied as unkeyed edits; the result lists them, for the caller to key.
		static std::expected<ProjectEx::RigResult, std::string> ApplyRig(const PendingRig& a_rig, ProjectEx& a_project, ProjectEx::ObjectId a_actor, const ProjectEx::RigOptions& a_options);

	private:
		// Any object but an actor, with its bones resolved against its actor's skeleton. Locators are left in scene
		// space; their parents are attached by the caller. Null, with a warning, for a modifier whose bones don't fit.
		static std::unique_ptr<Animation::Studio::SceneObject> BuildObject(const FileObject& a_object, const std::function<const ozz::animation::Skeleton&(ProjectEx::ObjectId)>& a_skeleton,
			const std::function<std::optional<int32_t>(ProjectEx::ObjectId, const std::string&)>& a_findBone, std::vector<std::string>& a_warnings);
	};
}
