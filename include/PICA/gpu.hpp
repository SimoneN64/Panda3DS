#pragma once
#include <array>

#include "config.hpp"
#include "helpers.hpp"
#include "logger.hpp"
#include "memory.hpp"
#include "PICA/float_types.hpp"
#include "PICA/regs.hpp"
#include "PICA/shader_unit.hpp"
#include "PICA/dynapica/shader_rec.hpp"
#include "renderer_gl/renderer_gl.hpp"
#include "PICA/pica_vertex.hpp"

class GPU {
	static constexpr u32 regNum = 0x300;
	using vec4f = OpenGL::Vector<Floats::f24, 4>;
	using Registers = std::array<u32, regNum>;

	Memory& mem;
	EmulatorConfig& config;
	ShaderUnit shaderUnit;
	ShaderJIT shaderJIT; // Doesn't do anything if JIT is disabled or not supported

	u8* vram = nullptr;
	MAKE_LOG_FUNCTION(log, gpuLogger)

	static constexpr u32 maxAttribCount = 12; // Up to 12 vertex attributes
	static constexpr u32 vramSize = u32(6_MB);
	Registers regs; // GPU internal registers
	std::array<vec4f, 16> currentAttributes; // Vertex attributes before being passed to the shader

	std::array<vec4f, 16> immediateModeAttributes; // Vertex attributes uploaded via immediate mode submission
	std::array<PICA::Vertex, 3> immediateModeVertices;
	uint immediateModeVertIndex;
	uint immediateModeAttrIndex; // Index of the immediate mode attribute we're uploading

	template <bool indexed, bool useShaderJIT>
	void drawArrays();

	// Silly method of avoiding linking problems. TODO: Change to something less silly
	void drawArrays(bool indexed);

	struct AttribInfo {
		u32 offset = 0; // Offset from base vertex array
		int size = 0; // Bytes per vertex
		u32 config1 = 0;
		u32 config2 = 0;
		u32 componentCount = 0; // Number of components for the attribute

		u64 getConfigFull() {
			return u64(config1) | (u64(config2) << 32);
		}
	};

	u64 getVertexShaderInputConfig() {
		return u64(regs[PICA::InternalRegs::VertexShaderInputCfgLow]) | (u64(regs[PICA::InternalRegs::VertexShaderInputCfgHigh]) << 32);
	}

	std::array<AttribInfo, maxAttribCount> attributeInfo; // Info for each of the 12 attributes
	u32 totalAttribCount = 0; // Number of vertex attributes to send to VS
	u32 fixedAttribMask = 0; // Which attributes are fixed?
	
	u32 fixedAttribIndex = 0; // Which fixed attribute are we writing to ([0, 11] range)
	u32 fixedAttribCount = 0; // How many attribute components have we written? When we get to 4 the attr will actually get submitted
	std::array<u32, 3> fixedAttrBuff; // Buffer to hold fixed attributes in until they get submitted

	// Command processor pointers for GPU command lists
	u32* cmdBuffStart = nullptr;
	u32* cmdBuffEnd = nullptr;
	u32* cmdBuffCurr = nullptr;

	Renderer renderer;
	PICA::Vertex getImmediateModeVertex();

  public:
	// 256 entries per LUT with each LUT as its own row forming a 2D image 256 * LUT_COUNT
	// Encoded in PICA native format
	static constexpr size_t LightingLutSize = PICA::Lights::LUT_Count * 256;
	std::array<uint32_t, LightingLutSize> lightingLUT;

	// Used to prevent uploading the lighting_lut on every draw call
	// Set to true when the CPU writes to the lighting_lut
	// Set to false by the renderer when the lighting_lut is uploaded ot the GPU
	bool lightingLUTDirty = false;

	GPU(Memory& mem, GLStateManager& gl, EmulatorConfig& config);
	void initGraphicsContext() { renderer.initGraphicsContext(); }
	void getGraphicsContext() { renderer.getGraphicsContext(); }
	void display() { renderer.display(); }
	void screenshot(const std::string& name) { renderer.screenshot(name); }

	void fireDMA(u32 dest, u32 source, u32 size);
	void reset();

	Registers& getRegisters() { return regs; }
	void startCommandList(u32 addr, u32 size);

	// Used by the GSP GPU service for readHwRegs/writeHwRegs/writeHwRegsMasked
	u32 readReg(u32 address);
	void writeReg(u32 address, u32 value);

	// Used when processing GPU command lists
	u32 readInternalReg(u32 index);
	void writeInternalReg(u32 index, u32 value, u32 mask);

	// TODO: Emulate the transfer engine & its registers
	// Then this can be emulated by just writing the appropriate values there
	void clearBuffer(u32 startAddress, u32 endAddress, u32 value, u32 control) {
		renderer.clearBuffer(startAddress, endAddress, value, control);
	}

	// TODO: Emulate the transfer engine & its registers
	// Then this can be emulated by just writing the appropriate values there
	void displayTransfer(u32 inputAddr, u32 outputAddr, u32 inputSize, u32 outputSize, u32 flags) {
		renderer.displayTransfer(inputAddr, outputAddr, inputSize, outputSize, flags);
	}

	// Read a value of type T from physical address paddr
	// This is necessary because vertex attribute fetching uses physical addresses
	template <typename T>
	T readPhysical(u32 paddr) {
		if (paddr >= PhysicalAddrs::FCRAM && paddr <= PhysicalAddrs::FCRAMEnd) {
			u8* fcram = mem.getFCRAM();
			u32 index = paddr - PhysicalAddrs::FCRAM;

			return *(T*)&fcram[index];
		} else {
			Helpers::panic("[PICA] Read unimplemented paddr %08X", paddr);
		}
	}

	// Get a pointer of type T* to the data starting from physical address paddr
	template <typename T>
	T* getPointerPhys(u32 paddr) {
		if (paddr >= PhysicalAddrs::FCRAM && paddr <= PhysicalAddrs::FCRAMEnd) {
			u8* fcram = mem.getFCRAM();
			u32 index = paddr - PhysicalAddrs::FCRAM;

			return (T*)&fcram[index];
		} else if (paddr >= PhysicalAddrs::VRAM && paddr <= PhysicalAddrs::VRAMEnd) {
			u32 index = paddr - PhysicalAddrs::VRAM;
			return (T*)&vram[index];
		} else [[unlikely]] {
			Helpers::panic("[GPU] Tried to access unknown physical address: %08X", paddr);
		}
	}
};