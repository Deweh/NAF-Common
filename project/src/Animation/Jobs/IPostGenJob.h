#pragma once

namespace Animation
{
	class IPostGenJob
	{
	public:
		class Owner
		{
		public:
			Owner(const Owner&) = delete;
			Owner& operator=(const Owner&) = delete;

			inline Owner(IPostGenJob* a_ptr)
			{
				_impl = a_ptr;
			}

			inline Owner(Owner&& a_other)
			{
				_impl = a_other.release();
			}

			inline void operator=(Owner&& a_other)
			{
				_impl = a_other.release();
			}

			inline constexpr IPostGenJob* get() const
			{
				return _impl;
			}

			inline IPostGenJob* operator->() const
			{
				return get();
			}

			inline IPostGenJob* release()
			{
				IPostGenJob* ptr = _impl;
				_impl = nullptr;
				return ptr;
			}

			inline void reset(IPostGenJob* a_ptr = nullptr)
			{
				if (_impl) {
					_impl->Destroy();
				}
				_impl = a_ptr;
			}

			inline ~Owner()
			{
				reset();
			}
		private:
			IPostGenJob* _impl{ nullptr };
		};

		struct Context
		{
			ozz::math::SoaTransform* localTransforms;
			uint64_t localCount;
			ozz::math::Float4x4* modelSpaceMatrices;
			uint64_t modelSpaceCount;
			ozz::math::Float4x4 rootMatrix;
			ozz::math::Float4x4 prevRootMatrix;
			ozz::math::Float4x4 invertedRootMatrix;
			ozz::animation::Skeleton* skeleton;
			float deltaTime;
		};

		union GUID
		{
			struct
			{
				uint32_t jobType;
				uint32_t instanceNum;
			} parts;
			uint64_t full;
		};

		virtual bool Run(const Context& a_context) = 0;
		virtual GUID GetGUID() = 0;
		virtual void Destroy() = 0;
	};
}