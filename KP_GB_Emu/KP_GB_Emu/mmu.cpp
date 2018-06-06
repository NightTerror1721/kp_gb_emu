#include "mmu.h"

#include <cstring>

typedef DWORD ext_address;


Memory::Memory(const Kernel* core) : core(core)
{
	this->wram = new BYTE[WRAM_PAGE_LEN * (core->cartridge->isColorGB() ? 8 : 2)];
	this->vram = new BYTE[VRAM_PAGE_LEN * (core->cartridge->isColorGB() ? 2 : 1)];
}


Memory::~Memory()
{
	delete[] wram;
	delete[] vram;
}

bool Memory::hasBatery() const
{
	return core->cartridge->hasBattery();
}

void Memory::save(std::ofstream& output) const
{
	core->cpush_error("no battery");
}

void Memory::load(std::ifstream& input)
{
	core->cpush_error("no battery");
}


void Memory::setValue(address h_addr, BYTE value)
{
	ext_address addr = h_addr;
	switch(addr & 0xF000)
	{
		case 0x0000:
		case 0x1000:
		case 0x2000:
		case 0x3000:
		case 0x4000:
		case 0x5000:
		case 0x6000:
		case 0x7000:
			// handled by external hardware, if any
			break;
		case 0x8000:
		case 0x9000:
			vram[vramPageStart + addr - 0x8000] = value;
			break;
		case 0xA000:
		case 0xB000:
			// handled by external hardware, if any
			break;
		case 0xC000:
			wram[addr - 0xC000] = value;
			break;
		case 0xD000:
			wram[wramPageStart + addr - 0xD000] = value;
			break;
		case 0xE000:
		case 0xF000:
			// FEA0-FEFF is not usable
			if(0xFEA0 <= addr && addr <= 0xFEFF)
				break;
			if (addr < 0xFE00) // 7.5kb echo
				setValue(h_addr - 0xE000, value);
			else if (addr < 0xFF00)
				oam[addr - 0xFE00] = value;
			else setIO(h_addr - 0xFF00, value);
			break;
	}
}

BYTE Memory::getValue(address h_addr) const
{
	ext_address addr = h_addr;
	switch(addr & 0xF000)
	{
		case 0x0000:
		case 0x1000:
		case 0x2000:
		case 0x3000:
			return core->cartridge->romdata(addr);
		case 0x4000:
		case 0x5000:
		case 0x6000:
		case 0x7000:
			return core->cartridge->romdata(romPageStart + addr - 0x4000);
		case 0x8000:
		case 0x9000:
			return vram[vramPageStart + addr - 0x8000];
		case 0xA000:
		case 0xB000:
			return 0;
		case 0xC000:
			return wram[addr - 0xC000];
		case 0xD000:
			return wram[wramPageStart + addr - 0xD000];
		case 0xE000:
		case 0xF000:
			// FEA0-FEFF is not usable
			if (0xFEA0 <= addr && addr <= 0xFEFF)
				return 0xFF;
			else if (addr < 0xFE00)
				// E000-FE00 echoes the main ram
				// But wait, E000-FE00 contains just 7.5kb and hence
				// does not echo the entire 8kb internal ram
				return getValue(addr - 0xE000);
			else if (addr < 0xFF00)
				return oam[addr - 0xFF00];
			else return getIO(addr - 0xFF00);
	}
	return 0xFF;
}

















/* Internal Classes */
class MBC : protected Memory
{
protected:
	DWORD ramPageStart;

	FLAG ramEnabled;

	BYTE* cartRam;
	const DWORD cartRamSize;

public:
	MBC(const Kernel* core, DWORD cartRamSize) : Memory(core), ramPageStart(0), ramEnabled(false), cartRamSize(cartRamSize)
	{
		this->cartRam = new BYTE[cartRamSize];
	}
	~MBC()
	{
		delete[] cartRam;
	}

	void save(std::ofstream& output) const
	{
		if (!hasBatery())
		{
			core->cpush_error("no battery");
			return;
		}
		output.write(to_raw(cartRam), cartRamSize);
	}

	void load(std::ifstream& input)
	{
		if (!hasBatery())
		{
			core->cpush_error("no battery");
			return;
		}
		
		RAW_VALUE* raw = to_raw(cartRam);
		if (input)
		{
			std::memset(raw, 0, cartRamSize);
			input.read(raw, cartRamSize);
			if (input.gcount() != cartRamSize)
				core->cpush_error("cart data invalid");

		}
		else core->cpush_error("Premature end of input stream");
	}

	BYTE getValue(address h_addr) const
	{
		ext_address addr = h_addr;
		switch (addr)
		{
			case 0xA000:
			case 0xB000:
				if (ramEnabled)
					return cartRam[addr - 0xA000 + ramPageStart];
				return 0xFF;
		}
		return Memory::getValue(h_addr);
	}
};


class MBC1 : MBC
{
private:
	FLAG modeSelect;

	WORD romBank;

	MBC1(const Kernel* core) : MBC(core, RAM_PAGE_LEN * 4) {}
	~MBC1() = default;

	void mapRom(WORD bank)
	{
		switch(bank) { case 0x00: case 0x20: case 0x40: case 0x60: bank++; }
		romBank = bank;
		romPageStart = ROM_PAGE_LEN * bank;
	}

	void setValue(address h_addr, BYTE value)
	{
		ext_address addr = h_addr;
		switch (addr)
		{
			case 0x0000:
			case 0x1000:
				if (core->cartridge->getRamBanks() > 0)
					ramEnabled = (value & 0x0F) == 0x0A;
				break;
			case 0xA000:
			case 0xB000:
				if (ramEnabled)
					cartRam[addr - 0xA000 + ramPageStart] = value;
				break;
			case 0x2000:
			case 0x3000:
				mapRom((romBank & 0x60) | (value & 0x1F));
				break;
			case 0x4000:
			case 0x5000:
				if (!modeSelect)
					ramPageStart = (value & 0x03) * RAM_PAGE_LEN;
				else mapRom((romBank & 0x1F) | ((value & 0x03) << 4));
				break;
			case 0x6000:
			case 0x7000:
				if (core->cartridge->getRamBanks() == 3)
					modeSelect = (value & 0x01);
				break;
			default:
				MBC::setValue(h_addr, value);
				break;
		}
	}
};


class MBC2 : MBC
{
	MBC2(const Kernel* core) : MBC(core, 1)
	{
		core->cpush_error("unsupported cartridge");
	}
	~MBC2() = default;
};


class MBC3 : MBC
{
private:
	int16 ramBank;

	FLAG rtcEnabled;

	BYTE rtc[4];

public:
	MBC3(const Kernel* core) : MBC(core, RAM_PAGE_LEN * 4) {}
	~MBC3() = default;

	void setValue(address h_addr, BYTE value)
	{
		ext_address addr = h_addr;
		switch (addr)
		{
			case 0x0000:
			case 0x1000:
				if (core->cartridge->getRamBanks() != 0)
					ramEnabled = (value & 0x0F) == 0x0A;
				rtcEnabled = (value & 0x0F) == 0x0A;
				break;
			case 0x2000:
			case 0x3000:
				romPageStart = ROM_PAGE_LEN * max((value & 0x7F), 1);
				break;
			case 0x4000:
			case 0x5000:
				if (value >= 0x08 && value <= 0x0C)
				{
					if (rtcEnabled)
						ramBank = -1;
				}
				else if (value <= 0x03)
				{
					ramBank = value;
					ramPageStart = ramBank * RAM_PAGE_LEN;
				}
				break;
			case 0xA000:
			case 0xB000:
				// FIXME this is probably incorrect
				rtc[ramBank - 0x08] = value;
				break;
			default:
				MBC::setValue(h_addr, value);
				break;
		}
	}
};

