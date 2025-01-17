#include "Scene.hpp"
#include "Model.hpp"
#include "Sphere.hpp"
#include "Texture.hpp"
#include "TextureImage.hpp"
#include "Vulkan/BufferUtil.hpp"
#include "Vulkan/ImageView.hpp"
#include "Vulkan/Sampler.hpp"
#include "Utilities/Exception.hpp"
#include "Vulkan/SingleTimeCommands.hpp"

#include <random>
#include <algorithm>
#include <fstream>

namespace Assets {

Scene::Scene(Vulkan::CommandPool& commandPool, std::vector<Model>&& models, std::vector<Texture>&& textures, bool usedForRayTracing) :
	models_(std::move(models)),
	textures_(std::move(textures))
{
	// Concatenate all the models
	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
	std::vector<Material> materials;
	std::vector<glm::vec4> procedurals;
	std::vector<VkAabbPositionsKHR> aabbs;
	std::vector<glm::uvec2> offsets;

	std::vector<uint32_t> threadSwizzle(1280 * 720);
	// std::vector<uint32_t> threadSwizzle(2560 * 1440);
	// #define USE_SWIZZLE
	#ifdef USE_SWIZZLE
	// std::ifstream file("identity_swizzle.csv");
	// std::ifstream file("random_swizzle.csv");
	// std::ifstream file("numrays_swizzle.csv");
	// std::ifstream file("objects_swizzle.csv");
	// std::ifstream file("tiled_numrays_swizzle.csv");
	std::ifstream file("tiled_objects_swizzle.csv");
	if (file.is_open()) {
		for (uint32_t thread_id = 0; thread_id < threadSwizzle.size(); thread_id++) {
			char comma;
			uint32_t swizzled_thread_id;
			file >> swizzled_thread_id >> comma;
			threadSwizzle[thread_id] = swizzled_thread_id;
		}
		file.close();
	} else {
		fprintf(stderr, "Failed to open swizzle.csv\n");
	}
	#else
	// Identity swizzle
	for (uint32_t thread_id = 0; thread_id < threadSwizzle.size(); thread_id++) {
		threadSwizzle[thread_id] = thread_id;
	}
	// // Randomize the identity swizzle
	// std::random_device rd;
	// std::default_random_engine rng(rd());
	// rng.seed(0);
	// std::shuffle(threadSwizzle.begin(), threadSwizzle.end(), rng);
	#endif


	for (const auto& model : models_)
	{
		// Remember the index, vertex offsets.
		const auto indexOffset = static_cast<uint32_t>(indices.size());
		const auto vertexOffset = static_cast<uint32_t>(vertices.size());
		const auto materialOffset = static_cast<uint32_t>(materials.size());

		offsets.emplace_back(indexOffset, vertexOffset);

		// Copy model data one after the other.
		vertices.insert(vertices.end(), model.Vertices().begin(), model.Vertices().end());
		indices.insert(indices.end(), model.Indices().begin(), model.Indices().end());
		materials.insert(materials.end(), model.Materials().begin(), model.Materials().end());

		// Adjust the material id.
		for (size_t i = vertexOffset; i != vertices.size(); ++i)
		{
			vertices[i].MaterialIndex += materialOffset;
		}

		// Add optional procedurals.
		const auto* const sphere = dynamic_cast<const Sphere*>(model.Procedural());
		if (sphere != nullptr)
		{
			const auto aabb = sphere->BoundingBox();
			aabbs.push_back({aabb.first.x, aabb.first.y, aabb.first.z, aabb.second.x, aabb.second.y, aabb.second.z});
			procedurals.emplace_back(sphere->Center, sphere->Radius);
		}
		else
		{
			aabbs.emplace_back();
			procedurals.emplace_back();
		}
	}

	const auto flag = usedForRayTracing ? VK_BUFFER_USAGE_STORAGE_BUFFER_BIT  | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT : 0;

	Vulkan::BufferUtil::CreateDeviceBuffer(commandPool, "Vertices", VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | flag, vertices, vertexBuffer_, vertexBufferMemory_);
	Vulkan::BufferUtil::CreateDeviceBuffer(commandPool, "Indices", VK_BUFFER_USAGE_INDEX_BUFFER_BIT | flag, indices, indexBuffer_, indexBufferMemory_);
	Vulkan::BufferUtil::CreateDeviceBuffer(commandPool, "Materials", flag, materials, materialBuffer_, materialBufferMemory_);
	Vulkan::BufferUtil::CreateDeviceBuffer(commandPool, "Offsets", flag, offsets, offsetBuffer_, offsetBufferMemory_);

	Vulkan::BufferUtil::CreateDeviceBuffer(commandPool, "AABBs", flag, aabbs, aabbBuffer_, aabbBufferMemory_);
	Vulkan::BufferUtil::CreateDeviceBuffer(commandPool, "Procedurals", flag, procedurals, proceduralBuffer_, proceduralBufferMemory_);

	Vulkan::BufferUtil::CreateDeviceBuffer(commandPool, "ThreadSwizzle", flag, threadSwizzle, threadSwizzleBuffer_, threadSwizzleBufferMemory_);

	
	// Upload all textures
	textureImages_.reserve(textures_.size());
	textureImageViewHandles_.resize(textures_.size());
	textureSamplerHandles_.resize(textures_.size());

	for (size_t i = 0; i != textures_.size(); ++i)
	{
	   textureImages_.emplace_back(new TextureImage(commandPool, textures_[i]));
	   textureImageViewHandles_[i] = textureImages_[i]->ImageView().Handle();
	   textureSamplerHandles_[i] = textureImages_[i]->Sampler().Handle();
	}
}

Scene::~Scene()
{
	textureSamplerHandles_.clear();
	textureImageViewHandles_.clear();
	textureImages_.clear();
	threadSwizzleBuffer_.reset();
	threadSwizzleBufferMemory_.reset(); // release memory after bound buffer has been destroyed
	proceduralBuffer_.reset();
	proceduralBufferMemory_.reset(); // release memory after bound buffer has been destroyed
	aabbBuffer_.reset();
	aabbBufferMemory_.reset(); // release memory after bound buffer has been destroyed
	offsetBuffer_.reset();
	offsetBufferMemory_.reset(); // release memory after bound buffer has been destroyed
	materialBuffer_.reset();
	materialBufferMemory_.reset(); // release memory after bound buffer has been destroyed
	indexBuffer_.reset();
	indexBufferMemory_.reset(); // release memory after bound buffer has been destroyed
	vertexBuffer_.reset();
	vertexBufferMemory_.reset(); // release memory after bound buffer has been destroyed
}

}
