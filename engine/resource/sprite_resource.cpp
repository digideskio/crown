/*
Copyright (c) 2013 Daniele Bartolini, Michele Rossi
Copyright (c) 2012 Daniele Bartolini, Simone Boscaratto

Permission is hereby granted, free of charge, to any person
obtaining a copy of this software and associated documentation
files (the "Software"), to deal in the Software without
restriction, including without limitation the rights to use,
copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following
conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.
*/

#include "allocator.h"
#include "filesystem.h"
#include "string_utils.h"
#include "json_parser.h"
#include "sprite_resource.h"
#include "string_utils.h"
#include "array.h"
#include "config.h"
#include "reader_writer.h"
#include <cfloat>
#include <cstring>
#include <inttypes.h>

namespace crown
{
namespace sprite_resource
{
	struct SpriteFrame
	{
		StringId32 name;
		Vector4 region;		// [x0, y0, x1, y1]
		Vector2 scale;		// [Sx, Sy]
		Vector2 offset;		// [Ox, Oy]
	};

	void parse_frame(JSONElement e, SpriteFrame& frame)
	{
		frame.name   = e.key("name"  ).to_string_id();
		frame.region = e.key("region").to_vector4();
		frame.offset = e.key("offset").to_vector2();
		frame.scale  = e.key("scale" ).to_vector2();
	}

	void compile(Filesystem& fs, const char* resource_path, File* out_file)
	{
		File* file = fs.open(resource_path, FOM_READ);
		JSONParser json(*file);
		fs.close(file);

		JSONElement root = json.root();

		// Read width/height
		const float width  = root.key("width" ).to_float();
		const float height = root.key("height").to_float();
		const uint32_t num_frames = root.key("frames").size();


		BinaryWriter bw(*out_file);

		Array<float> vertices(default_allocator());
		Array<uint16_t> indices(default_allocator());
		uint32_t num_idx = 0;
		for (uint32_t i = 0; i < num_frames; i++)
		{
			JSONElement e(root.key("frames")[i]);

			SpriteFrame frame;
			parse_frame(e, frame);

			const SpriteFrame& fd = frame;

			// Compute uv coords
			const float u0 = fd.region.x / width;
			const float v0 = fd.region.y / height;
			const float u1 = (fd.region.x + fd.region.z) / width;
			const float v1 = (fd.region.y + fd.region.w) / height;

			// Compute positions
			const float w = fd.region.z / CE_PIXELS_PER_METER;
			const float h = fd.region.w / CE_PIXELS_PER_METER;

			const float x0 = fd.scale.x * (-w * 0.5f) + fd.offset.x;
			const float y0 = fd.scale.y * (-h * 0.5f) + fd.offset.y;
			const float x1 = fd.scale.x * ( w * 0.5f) + fd.offset.x;
			const float y1 = fd.scale.y * ( h * 0.5f) + fd.offset.y;

			array::push_back(vertices, x0); array::push_back(vertices, y0); // position
			array::push_back(vertices, u0); array::push_back(vertices, v0); // uv

			array::push_back(vertices, x1); array::push_back(vertices, y0); // position
			array::push_back(vertices, u1); array::push_back(vertices, v0); // uv

			array::push_back(vertices, x1); array::push_back(vertices, y1); // position
			array::push_back(vertices, u1); array::push_back(vertices, v1); // uv

			array::push_back(vertices, x0); array::push_back(vertices, y1); // position
			array::push_back(vertices, u0); array::push_back(vertices, v1); // uv

			array::push_back(indices, uint16_t(num_idx)); array::push_back(indices, uint16_t(num_idx + 1)); array::push_back(indices, uint16_t(num_idx + 2));
			array::push_back(indices, uint16_t(num_idx)); array::push_back(indices, uint16_t(num_idx + 2)); array::push_back(indices, uint16_t(num_idx + 3));
			num_idx += 4;
		}

		const uint32_t num_vertices = array::size(vertices) / 4; // 4 components per vertex
		const uint32_t num_indices = array::size(indices);

		// Write header
		bw.write(SPRITE_VERSION);
		bw.write(num_vertices);
		if (array::size(vertices))
			bw.write(array::begin(vertices), sizeof(float) * array::size(vertices));
		bw.write(num_indices);
		if (array::size(indices))
			bw.write(array::begin(indices), sizeof(uint16_t) * array::size(indices));
	}

	void* load(Allocator& allocator, Bundle& bundle, ResourceId id)
	{
		File* file = bundle.open(id);
		BinaryReader br(*file);

		uint32_t version;
		br.read(version);

		uint32_t num_verts;
		br.read(num_verts);
		const bgfx::Memory* vbmem = bgfx::alloc(num_verts * sizeof(float) * 4);
		br.read(vbmem->data, num_verts * sizeof(float) * 4);

		uint32_t num_inds;
		br.read(num_inds);
		const bgfx::Memory* ibmem = bgfx::alloc(num_inds * sizeof(uint16_t));
		br.read(ibmem->data, num_inds * sizeof(uint16_t));

		bundle.close(file);

		SpriteResource* so = (SpriteResource*) default_allocator().allocate(sizeof(SpriteResource));
		so->vbmem = vbmem;
		so->ibmem = ibmem;

		return so;
	}

	void online(StringId64 id, ResourceManager& rm)
	{
		ResourceId res_id;
		res_id.type = SPRITE_TYPE;
		res_id.name = id;

		SpriteResource* so = (SpriteResource*) rm.get(res_id);

		bgfx::VertexDecl decl;
		decl.begin()
			.add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
			.add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float, false)
			.end();

		so->vb = bgfx::createVertexBuffer(so->vbmem, decl);
		so->ib = bgfx::createIndexBuffer(so->ibmem);
	}

	void offline(StringId64 id, ResourceManager& rm)
	{
		ResourceId res_id;
		res_id.type = SPRITE_TYPE;
		res_id.name = id;

		SpriteResource* so = (SpriteResource*) rm.get(res_id);

		bgfx::destroyVertexBuffer(so->vb);
		bgfx::destroyIndexBuffer(so->ib);
	}

	void unload(Allocator& a, void* resource)
	{
		a.deallocate(resource);
	}
} // namespace sprite_resource

namespace sprite_animation_resource
{
	void parse_animations(JSONElement e, Array<SpriteAnimationName>& names, Array<SpriteAnimationData>& anim_data, Array<uint32_t>& frames)
	{
		const uint32_t num = e.key("animations").size();
		for (uint32_t i = 0; i < num; i++)
		{
			JSONElement anim(e.key("animations")[i]);

			SpriteAnimationName san;
			san.id = anim.key("name").to_string_id();

			const uint32_t num_frames = anim.key("frames").size();
			SpriteAnimationData sad;
			sad.num_frames = num_frames;
			sad.first_frame = array::size(frames);
			sad.time = anim.key("time").to_float();

			// Read frames
			for (uint32_t ff = 0; ff < num_frames; ff++)
				array::push_back(frames, (uint32_t) anim.key("frames")[ff].to_int());

			array::push_back(names, san);
			array::push_back(anim_data, sad);
		}
	}

	void compile(Filesystem& fs, const char* resource_path, File* out_file)
	{
		File* file = fs.open(resource_path, FOM_READ);
		JSONParser json(*file);
		fs.close(file);

		Array<SpriteAnimationName> anim_names(default_allocator());
		Array<SpriteAnimationData> anim_data(default_allocator());
		Array<uint32_t> anim_frames(default_allocator());

		parse_animations(json.root(), anim_names, anim_data, anim_frames);

		BinaryWriter bw(*out_file);
		bw.write(uint32_t(1)); // version
		bw.write(uint32_t(array::size(anim_names)));
		bw.write(uint32_t(array::size(anim_frames)));
		bw.write(uint32_t(
			sizeof(SpriteAnimationResource) +
			sizeof(SpriteAnimationName) * array::size(anim_names) +
			sizeof(SpriteAnimationData) * array::size(anim_data)));

		if (array::size(anim_names));
			bw.write(array::begin(anim_names), sizeof(SpriteAnimationName) * array::size(anim_names));
		if (array::size(anim_data))
			bw.write(array::begin(anim_data), sizeof(SpriteAnimationData) * array::size(anim_data));
		if (array::size(anim_frames))
			bw.write(array::begin(anim_frames), sizeof(uint32_t) * array::size(anim_frames));
	}

	void* load(Allocator& allocator, Bundle& bundle, ResourceId id)
	{
		File* file = bundle.open(id);
		const size_t file_size = file->size();
		void* res = allocator.allocate(file_size);
		file->read(res, file_size);
		bundle.close(file);
		return res;
	}

	void online(StringId64 /*id*/, ResourceManager& /*rm*/)
	{
	}

	void offline(StringId64 /*id*/, ResourceManager& /*rm*/)
	{
	}

	void unload(Allocator& a, void* resource)
	{
		a.deallocate(resource);
	}

	const SpriteAnimationData* get_animation(const SpriteAnimationResource* sar, StringId32 name)
	{
		const uint32_t num = sar->num_animations;
		const SpriteAnimationName* begin = (SpriteAnimationName*) ((char*) sar + sizeof(*sar));
		const SpriteAnimationData* data = (SpriteAnimationData*) ((char*) sar + sizeof(*sar) + sizeof(SpriteAnimationName) * num);

		for (uint32_t i = 0; i < num; i++)
		{
			if (begin[i].id == name)
				return &data[i];
		}

		return NULL;
	}

	const uint32_t* get_animation_frames(const SpriteAnimationResource* sar)
	{
		return (uint32_t*) ((char*) sar + sar->frames_offset);
	}
} // namespace sprite_animation_resource
} // namespace crown
