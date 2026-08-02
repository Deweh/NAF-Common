#pragma once

namespace Animation::Studio
{
	// Final local-space pose of an actor, double buffered. Evaluation writes the back buffer on the Studio's thread
	// and publishes it with Swap. The actor's StudioGenerator copies the front buffer each graph update, on the
	// game's animation thread, so it never sees a partly solved pose (FK without its IK).
	class SharedPose
	{
	public:
		using Buffer = std::vector<ozz::math::SoaTransform>;

		explicit SharedPose(Buffer a_initial) :
			m_buffers{ a_initial, std::move(a_initial) }
		{}

		// Studio thread only. m_front only changes on this thread, so reading it here needs no lock.
		[[nodiscard]] Buffer& Back() { return m_buffers[m_front ^ 1]; }
		[[nodiscard]] const Buffer& Front() const { return m_buffers[m_front]; }  // The last published pose.
		void Swap()
		{
			std::scoped_lock lock{ m_lock };
			m_front ^= 1;
		}

		// Any thread. Holds the lock while reading, so the front buffer can't become the back buffer meanwhile.
		template <class F>
		void ReadFront(F&& a_reader) const
		{
			std::scoped_lock lock{ m_lock };
			a_reader(m_buffers[m_front]);
		}

		// The bone mask the game applies to the pose (true = written to the game). Changes rarely, so readers keep a
		// copy and only read it again when a_version is behind.
		void SetMask(std::vector<bool> a_mask)
		{
			std::scoped_lock lock{ m_lock };
			m_mask = std::move(a_mask);
			++m_maskVersion;
		}

		template <class F>
		void ReadMask(uint32_t& a_version, F&& a_reader) const
		{
			std::scoped_lock lock{ m_lock };
			if (a_version == m_maskVersion) return;
			a_version = m_maskVersion;
			a_reader(m_mask);
		}

	private:
		std::array<Buffer, 2> m_buffers;
		uint8_t m_front{ 0 };
		std::vector<bool> m_mask;
		uint32_t m_maskVersion{ 0 };
		mutable std::mutex m_lock;
	};
}
