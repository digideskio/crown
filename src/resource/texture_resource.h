/*
 * Copyright (c) 2012-2016 Daniele Bartolini and individual contributors.
 * License: https://github.com/taylor001/crown/blob/master/LICENSE
 */

#pragma once

#include "memory_types.h"
#include "resource_types.h"
#include "filesystem_types.h"
#include "memory_types.h"
#include "compiler_types.h"
#include <bgfx/bgfx.h>

namespace crown
{
struct TextureHeader
{
	u32 version;
	u32 size;
};

struct TextureResource
{
	const bgfx::Memory* mem; // BGFX will take care of deallocation
	bgfx::TextureHandle handle;
};

namespace texture_resource
{
	void compile(const char* path, CompileOptions& opts);
	void* load(File& file, Allocator& a);
	void offline(StringId64 id, ResourceManager& rm);
	void online(StringId64 id, ResourceManager& rm);
	void unload(Allocator& a, void* resource);
} // namespace texture_resource
} // namespace crown
