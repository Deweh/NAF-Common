#include <Util/General.h>

namespace Animation
{
    enum InterpolationType
    {
        kLinear,
        kBezier,
        kStep
    };

    class StudioProject
    {
    public:
        template <typename T>
        struct InterpolationData
        {
        };

        template <>
        struct InterpolationData<ozz::math::Float3>
        {
            InterpolationType type;
            
        };

        template <typename T>
        struct Keyframe
        {
            float time;
            T value;
            InterpolationData<T> interp;
        };

        struct Clip
        {
            std::vector<Keyframe<ozz::math::Float3>> translationKeys;
            std::vector<Keyframe<ozz::math::Quaternion>> rotationKeys;
            std::vector<Keyframe<ozz::math::Float3>> scaleKeys;

            std::vector<ozz::math::SoaTransform> GetPoseAtTime(float a_time) const
            {
                
            }
        };

        struct ManagedActor
        {
            uint64_t uid;
            RE::NiPointer<RE::Actor> actor;
            Util::Guarded<std::unique_ptr<ozz::animation::Animation>, std::shared_mutex> cachedRuntime;
            Clip clip;
        };

        class ChangeAction
        {
        public:
            virtual ~ChangeAction() = default;
            virtual void Undo(StudioProject& a_project) = 0;
            virtual void Redo(StudioProject& a_project) = 0;
            virtual std::string GetDescription() const = 0;
        };

        std::pair<bool, std::string> Undo()
        {
            if (undoStack.empty()) {
                return { false, "" };
            }
            auto action = std::move(undoStack.top());
            undoStack.pop();
            action->Undo(*this);
            std::string description = action->GetDescription();
            redoStack.push(std::move(action));
            return { true, description };
        }

        std::pair<bool, std::string> Redo()
        {
            if (redoStack.empty()) {
                return { false, "" };
            }
            auto action = std::move(redoStack.top());
            redoStack.pop();
            action->Redo(*this);
            std::string description = action->GetDescription();
            undoStack.push(std::move(action));
            return { true, description };
        }

        void PushAction(std::unique_ptr<ChangeAction> a_action)
        {
            undoStack.push(std::move(a_action));
            if(!redoStack.empty()){
                redoStack = {};
            }
        }

        uint64_t GetNextUID()
        {
            return nextUid++;
        }

        static bool IsSameTime(float a, float b){
            return std::abs(a - b) < 0.00416f; // 0.00416 ~ 240fps
        }

        std::string filePath;
        std::vector<ManagedActor> managedActors;
        std::stack<std::unique_ptr<ChangeAction>> undoStack;
        std::stack<std::unique_ptr<ChangeAction>> redoStack;
        uint64_t nextUid { 1 };
    };
}