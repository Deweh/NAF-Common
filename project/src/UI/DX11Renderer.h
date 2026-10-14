#pragma once
// Before Renderer.h, which undefines the "far" these headers use.
#include "d3d11.h"
#include "dxgi.h"

#include "UI/Renderer.h"

namespace UI
{
	// Fallout 4's renderer.
	class DX11Renderer : public Renderer
	{
	public:
		ID3D11Device* m_d3dDevice{ nullptr };
		ID3D11DeviceContext* m_d3dDeviceContext{ nullptr };
		IDXGISwapChain* m_swapChain{ nullptr };
		ID3D11RenderTargetView* m_backBufferView{ nullptr };

		static DX11Renderer* GetSingleton();

		void Install() override;
		bool Initialize(IDXGISwapChain* a_swapChain);
		void Shutdown();
		void OnFrame();
		void OnResize();
	};
}
