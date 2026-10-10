#include "engine/fbx_importer.hpp"
#include "engine/mesh_optimizer.hpp"
#include "fbx_animation_import.hpp"
#include <ufbx.h>
#include <limits>
#include <string>
#include <unordered_map>
#include <chrono>
#include <cstring>
#ifndef __EMSCRIPTEN__
#include <fstream>
#endif

namespace engine {
namespace {
using ImportClock=std::chrono::steady_clock;
double elapsedMs(ImportClock::time_point start) { return std::chrono::duration<double,std::milli>(ImportClock::now()-start).count(); }
Vec3 checkedVector(ufbx_vec3 value) {
    const Vec3 out{static_cast<float>(value.x),static_cast<float>(value.y),static_cast<float>(value.z)};
    if(!std::isfinite(out.x) || !std::isfinite(out.y) || !std::isfinite(out.z))
        throw std::runtime_error("FBX contains non-finite or out-of-range geometry");
    return out;
}
void validateOptions(const FbxImportOptions& options) {
    if(!options.maxFileBytes || !options.maxVertices || !options.maxIndices || !options.parserMemoryBytes
        || !options.maxTextureBytes || !options.maxAnimationBytes || !options.maxMorphTargets || !options.maxMorphBytes || !std::isfinite(options.animationSampleRate)
        || options.animationSampleRate<1 || options.animationSampleRate>120
        || !options.textureLimits.maxFileBytes || !options.textureLimits.maxDimension || !options.textureLimits.maxDecodedBytes
        || options.maxVertices>std::numeric_limits<std::uint32_t>::max()
        || options.maxIndices>std::numeric_limits<std::uint32_t>::max())
        throw std::invalid_argument("Invalid FBX import limits");
}
}
std::shared_ptr<const Mesh> FbxImporter::loadMemory(std::span<const std::byte> data,const FbxImportOptions& options) {
    const auto started=ImportClock::now();
    if(options.measurements) *options.measurements={};
    struct TotalTimer { FbxImportMeasurements* output;ImportClock::time_point start;~TotalTimer(){if(output) output->totalMs=elapsedMs(start);} } totalTimer{options.measurements,started};
    validateOptions(options);
    if(data.empty()) throw std::runtime_error("FBX file is empty");
    if(data.size()>options.maxFileBytes) throw std::runtime_error("FBX file exceeds the size limit");
    ufbx_load_opts opts{};
    opts.file_format=UFBX_FILE_FORMAT_FBX;
    opts.generate_missing_normals=true;
    opts.normalize_normals=true;
    opts.use_blender_pbr_material=true;
    opts.target_axes=ufbx_axes_right_handed_y_up;
    opts.target_unit_meters=1;
    opts.space_conversion=UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
    opts.skip_mesh_parts=true;
    opts.load_external_files=false;
    opts.index_error_handling=UFBX_INDEX_ERROR_HANDLING_ABORT_LOADING;
    opts.node_depth_limit=256;
    opts.temp_allocator.memory_limit=options.parserMemoryBytes;
    opts.result_allocator.memory_limit=options.parserMemoryBytes;
    ufbx_error error{};
    std::unique_ptr<ufbx_scene,decltype(&ufbx_free_scene)> scene(
        ufbx_load_memory(data.data(),data.size(),&opts,&error),ufbx_free_scene);
    if(options.measurements) options.measurements->parseMs=elapsedMs(started);
    if(!scene) {
        char description[1024]{}; ufbx_format_error(description,sizeof(description),&error);
        throw std::runtime_error(std::string("FBX parsing failed: ")+description);
    }
    auto result=std::make_shared<Mesh>();
    struct ImportedMorph { const ufbx_node* node;const ufbx_blend_shape* shape;std::size_t target; };
    std::vector<ImportedMorph> morphs;
    std::size_t morphStreams=0;
    for(const auto* node:scene->nodes) if(node->mesh) for(const auto* deformer:node->mesh->blend_deformers)
        for(const auto* channel:deformer->channels) {
            for(const auto* layer:scene->anim_layers) if(ufbx_find_anim_prop(layer,&channel->element,"DeformPercent")) {
                result->warnings.push_back("FBX morph weight animation is not imported; set component weights at runtime: "+std::string(channel->name.data,channel->name.length));break;
            }
            if(channel->keyframes.count>1) result->warnings.push_back("FBX in-between shapes imported as independent morph targets: "+std::string(channel->name.data,channel->name.length));
            for(const auto& key:channel->keyframes) {
                if(!key.shape) throw std::runtime_error("FBX morph references a missing shape");
                if(morphs.size()>=options.maxMorphTargets) throw std::runtime_error("FBX exceeds morph target limit");
                const auto* shape=key.shape;
                if(shape->offset_vertices.count!=shape->position_offsets.count || (shape->normal_offsets.count&&shape->normal_offsets.count!=shape->position_offsets.count)
                    || (shape->offset_weights.count&&shape->offset_weights.count!=shape->position_offsets.count))
                    throw std::runtime_error("FBX morph offset streams do not match");
                for(auto control:shape->offset_vertices) if(control>=node->mesh->num_vertices) throw std::runtime_error("FBX morph vertex outside source mesh");
                std::string name(channel->name.data,channel->name.length);
                if(name.empty()) name.assign(shape->name.data,shape->name.length);
                if(channel->keyframes.count>1) name+="/"+std::string(shape->name.data,shape->name.length);
                if(name.empty()) name="Morph";
                const auto base=name;
                for(std::size_t suffix=1;std::any_of(result->morphTargets.begin(),result->morphTargets.end(),[&](const auto& t){return t.name==name;});++suffix)
                    name=base+"#"+std::to_string(suffix);
                const auto weight=static_cast<float>(key.effective_weight);
                if(!std::isfinite(weight)) throw std::runtime_error("FBX morph default weight must be finite");
                morphs.push_back({node,shape,result->morphTargets.size()});
                result->morphTargets.push_back({std::move(name),{},{},{},weight});
                morphStreams+=shape->normal_offsets.count ? 3 : 2; // Include derived tangent offsets.
            }
        }
    auto checkMorphBudget=[&](std::size_t vertices) {
        if(morphStreams && vertices>options.maxMorphBytes/sizeof(Vec3)/morphStreams) throw std::runtime_error("FBX morphs exceed the decoded memory limit");
    };
    FbxSkinImport skin(*scene,options,*result);
    const auto convertStart=ImportClock::now(); double textureDecodeMs=0;
    std::unordered_map<const ufbx_material*,std::uint32_t> materialMap;
    std::unordered_map<const ufbx_texture*,std::shared_ptr<const Texture>> textureMap;
    std::vector<const ufbx_texture*> materialTextures;
    std::vector<std::vector<std::uint32_t>> materialIndices;
    std::size_t decodedBytes=0,totalIndices=0;
    auto fileMap=[&](const ufbx_material_map& map,const std::string& label) -> const ufbx_texture* {
        if(!map.texture||!map.texture_enabled) return nullptr;
        if(map.texture->file_textures.count==1) return map.texture->file_textures.data[0];
        result->warnings.push_back("Unsupported layered/procedural "+label+" texture");return nullptr;
    };
    auto decode=[&](const ufbx_texture* source,const std::string& label) -> std::shared_ptr<const Texture> {
        if(!source) return {};
        if(auto found=textureMap.find(source);found!=textureMap.end()) return found->second;
        auto path=source->relative_filename.length ? source->relative_filename : source->filename;
        std::string name(path.data,path.length);std::replace(name.begin(),name.end(),'\\','/');
        std::vector<std::byte> external;
        std::span<const std::byte> bytes(static_cast<const std::byte*>(source->content.data),source->content.size);
        if(bytes.empty()&&options.resolveTexture) { external=options.resolveTexture(name);bytes=external; }
        std::shared_ptr<const Texture> decoded;
        if(bytes.empty()) result->warnings.push_back("Missing "+label+" texture: "+name);
        else {
            if(decodedBytes>=options.maxTextureBytes) throw std::runtime_error("FBX textures exceed the total decoded size limit");
            auto limits=options.textureLimits;limits.maxDecodedBytes=std::min(limits.maxDecodedBytes,options.maxTextureBytes-decodedBytes);
            const auto started=ImportClock::now();auto image=std::make_shared<Texture>(*Texture::loadMemory(bytes,name,limits));
            textureDecodeMs+=elapsedMs(started);decodedBytes+=image->pixels.size();
            image->wrapU=source->wrap_u==UFBX_WRAP_CLAMP ? TextureWrap::Clamp : TextureWrap::Repeat;
            image->wrapV=source->wrap_v==UFBX_WRAP_CLAMP ? TextureWrap::Clamp : TextureWrap::Repeat;decoded=std::move(image);
            if(options.measurements) { options.measurements->textureDecodeMs=textureDecodeMs;options.measurements->decodedTextureBytes=decodedBytes;++options.measurements->decodedTextures; }
        }
        textureMap.emplace(source,decoded);return decoded;
    };
    auto materialIndex=[&](const ufbx_material* source) {
        if(auto found=materialMap.find(source);found!=materialMap.end()) return found->second;
        const auto index=static_cast<std::uint32_t>(result->materials.size());
        auto material=std::make_shared<Material>();const ufbx_texture* primary=nullptr;
        if(source) {
            material->name.assign(source->name.data,source->name.length);
            const auto& map=source->pbr.base_color.has_value||source->pbr.base_color.texture ? source->pbr.base_color : source->fbx.diffuse_color;
            auto scalar=[](const ufbx_material_map& m,float fallback) { return m.has_value ? float(m.value_real) : fallback; };
            if(map.has_value) material->baseColor=map.value_components>=3 ? checkedVector(map.value_vec3) : Vec3{float(map.value_real),float(map.value_real),float(map.value_real)};
            material->baseColor=material->baseColor*scalar(source->pbr.base_factor,scalar(source->fbx.diffuse_factor,1));
            primary=fileMap(map,"base-color");
            if(primary) {
                auto path=primary->relative_filename.length ? primary->relative_filename : primary->filename;
                material->texturePath.assign(path.data,path.length);std::replace(material->texturePath.begin(),material->texturePath.end(),'\\','/');
                material->baseColorTexture=decode(primary,"base-color");
            }
            // Geometry has one UV stream. Reject maps using incompatible UV transforms/sets.
            auto mappedTexture=[&](const ufbx_material_map& m,const std::string& label) -> std::shared_ptr<const Texture> {
                const auto* t=fileMap(m,label);if(!t) return {};
                if(!primary) primary=t;
                if(t!=primary && (std::string_view(t->uv_set.data,t->uv_set.length)!=std::string_view(primary->uv_set.data,primary->uv_set.length)
                    ||t->has_uv_transform!=primary->has_uv_transform||(t->has_uv_transform&&std::memcmp(&t->uv_to_texture,&primary->uv_to_texture,sizeof(ufbx_transform))))) {
                    result->warnings.push_back("Unsupported alternate UV set/transform for "+label+": "+material->name);return {};
                }
                return decode(t,label);
            };
            material->roughness=scalar(source->pbr.roughness,0.6f);
            material->metallic=scalar(source->pbr.metalness,0);
            material->opacity=scalar(source->pbr.opacity,1-scalar(source->fbx.transparency_factor,0));
            material->doubleSided=source->features.double_sided.enabled;
            material->shader=source->features.unlit.enabled ? MaterialShader::Unlit : MaterialShader::Pbr;
            const auto& emission=source->pbr.emission_color.has_value||source->pbr.emission_color.texture ? source->pbr.emission_color : source->fbx.emission_color;
            if(emission.has_value) material->emissive=checkedVector(emission.value_vec3);
            else if(emission.texture) material->emissive={1,1,1};
            material->emissive=material->emissive*scalar(source->pbr.emission_factor,scalar(source->fbx.emission_factor,1));
            material->emissiveTexture=mappedTexture(emission,"emissive");
            material->normalTexture=mappedTexture(source->pbr.normal_map.texture ? source->pbr.normal_map : source->fbx.normal_map,"normal");
            material->occlusionTexture=mappedTexture(source->pbr.ambient_occlusion,"occlusion");
            material->occlusionStrength=scalar(source->pbr.ambient_occlusion,1);
            const auto rough=mappedTexture(source->pbr.roughness,"roughness"),metal=mappedTexture(source->pbr.metalness,"metallic");
            if(rough||metal) {
                const auto basis=rough ? rough : metal;
                auto packed=std::make_shared<Texture>();packed->name=material->name+" metallic-roughness";
                packed->width=basis->width;packed->height=basis->height;packed->wrapU=basis->wrapU;packed->wrapV=basis->wrapV;
                const auto size=std::size_t(packed->width)*packed->height*4;
                if(size>options.maxTextureBytes-decodedBytes) throw std::runtime_error("Packed material textures exceed decoded size limit");
                packed->pixels.resize(size,255);decodedBytes+=size;
                auto channel=[](const Texture& t,std::uint32_t x,std::uint32_t y,std::uint32_t w,std::uint32_t h) {
                    return t.pixels[(std::size_t(std::uint64_t(y)*t.height/h)*t.width+std::uint64_t(x)*t.width/w)*4];
                };
                for(std::uint32_t y=0;y<packed->height;++y) for(std::uint32_t x=0;x<packed->width;++x) {
                    const auto offset=(std::size_t(y)*packed->width+x)*4;
                    if(rough) { const auto v=channel(*rough,x,y,packed->width,packed->height);packed->pixels[offset+1]=source->features.roughness_as_glossiness.enabled ? 255-v : v; }
                    if(metal) packed->pixels[offset+2]=channel(*metal,x,y,packed->width,packed->height);
                }
                material->metallicRoughnessTexture=packed;
                if(rough&&!source->pbr.roughness.has_value) material->roughness=1;
                if(metal&&!source->pbr.metalness.has_value) material->metallic=1;
                if(options.measurements) options.measurements->decodedTextureBytes=decodedBytes;
            }
            if(source->pbr.opacity.texture||source->fbx.transparency_color.texture) result->warnings.push_back("Separate opacity texture is unsupported; use base-color alpha: "+material->name);
            if(source->features.pbr.enabled&&source->pbr.transmission_factor.has_value&&source->pbr.transmission_factor.value_real>0) result->warnings.push_back("Transmission/refraction is unsupported: "+material->name);
            if(material->opacity<1) material->alphaMode=AlphaMode::Blend;
            else if(material->baseColorTexture) {
                bool alpha=false,partial=false;const auto& pixels=material->baseColorTexture->pixels;
                for(std::size_t i=3;i<pixels.size();i+=4) { alpha|=pixels[i]!=255;partial|=pixels[i]>0&&pixels[i]<255; }
                if(alpha) material->alphaMode=partial ? AlphaMode::Blend : AlphaMode::Mask;
            }
        }
        material->validate();result->materials.push_back(material);materialTextures.push_back(primary);
        materialIndices.emplace_back();materialMap.emplace(source,index);return index;
    };
    for(const auto* node:scene->nodes) {
        const auto* source=node->mesh;
        if(!source || source->num_triangles==0) continue;
        if(source->num_indices>options.maxVertices || source->max_face_triangles>options.maxIndices/3)
            throw std::runtime_error("FBX geometry exceeds the import limits");
        const double determinant=ufbx_matrix_determinant(&node->geometry_to_world);
        if(!std::isfinite(determinant) || determinant==0)
            throw std::runtime_error("FBX mesh node has a singular transform");
        const auto normalMatrix=ufbx_matrix_for_normals(&node->geometry_to_world);
        std::vector<std::uint32_t> triangles(source->max_face_triangles*3);
        std::vector<std::uint32_t> cornerMap(source->num_indices,std::numeric_limits<std::uint32_t>::max());
        for(std::size_t faceIndex=0;faceIndex<source->faces.count;++faceIndex) {
            const auto face=source->faces.data[faceIndex];
            if(face.num_indices<3) continue;
            if(faceIndex<source->face_hole.count && source->face_hole.data[faceIndex]) continue;
            const auto count=ufbx_triangulate_face(triangles.data(),triangles.size(),source,face);
            if(count==0) continue;
            if(count> (options.maxIndices-totalIndices)/3)
                throw std::runtime_error("FBX triangle count exceeds the import limit");
            const auto slot=faceIndex<source->face_material.count ? source->face_material.data[faceIndex] : UFBX_NO_INDEX;
            const auto material=materialIndex(slot<node->materials.count ? node->materials.data[slot] : nullptr);
            const auto* texture=materialTextures[material];
            const ufbx_vertex_vec2* uv=&source->vertex_uv;
            if(texture && texture->uv_set.length) {
                bool found=false;
                for(const auto& set:source->uv_sets) if(set.name.length==texture->uv_set.length
                    && std::string_view(set.name.data,set.name.length)==std::string_view(texture->uv_set.data,texture->uv_set.length)) { uv=&set.vertex_uv; found=true; break; }
                if(!found) {
                    const std::string warning="Texture UV set unavailable, using first UV set: "+result->materials[material]->name;
                    if(std::find(result->warnings.begin(),result->warnings.end(),warning)==result->warnings.end()) result->warnings.push_back(warning);
                }
            }
            if(texture && !uv->exists) {
                const std::string warning="Textured mesh has no UV coordinates: "+result->materials[material]->name;
                if(std::find(result->warnings.begin(),result->warnings.end(),warning)==result->warnings.end()) result->warnings.push_back(warning);
            }
            for(std::size_t triangle=0;triangle<count;++triangle) {
                std::uint32_t output[3]{};
                for(std::size_t corner=0;corner<3;++corner) {
                    const auto index=triangles[triangle*3+corner];
                    if(index>=source->num_indices) throw std::runtime_error("FBX triangulation returned an invalid corner");
                    auto& mapped=cornerMap[index];
                    if(mapped==std::numeric_limits<std::uint32_t>::max()) {
                        if(result->vertices.size()>=options.maxVertices)
                            throw std::runtime_error("FBX vertex count exceeds the import limit");
                        const auto position=ufbx_transform_position(&node->geometry_to_world,ufbx_get_vertex_vec3(&source->vertex_position,index));
                        const auto normal=ufbx_vec3_normalize(ufbx_transform_direction(&normalMatrix,ufbx_get_vertex_vec3(&source->vertex_normal,index)));
                        const auto n=checkedVector(normal);
                        if(dot(n,n)<0.5f) throw std::runtime_error("FBX contains a degenerate vertex normal");
                        mapped=static_cast<std::uint32_t>(result->vertices.size());
                        Vec2 texcoord{};
                        if(uv->exists) {
                            const auto sourceUV=ufbx_get_vertex_vec2(uv,index);
                            ufbx_vec3 converted{}; converted.x=sourceUV.x; converted.y=sourceUV.y;
                            if(texture && texture->has_uv_transform) converted=ufbx_transform_position(&texture->uv_to_texture,converted);
                            const auto finite=checkedVector(converted);
                            texcoord={finite.x,1-finite.y}; // FBX UVs are bottom-up; images are top-down.
                        }
                        result->vertices.push_back({checkedVector(position),n,texcoord});
                        checkMorphBudget(result->vertices.size());
                        for(const auto& morph:morphs) {
                            auto& target=result->morphTargets[morph.target];target.positions.push_back({});
                            if(morph.shape->normal_offsets.count) target.normals.push_back({});
                            if(morph.node!=node) continue;
                            const auto control=source->vertex_indices.data[index];
                            const auto offset=ufbx_get_blend_shape_offset_index(morph.shape,control);
                            if(offset==UFBX_NO_INDEX) continue;
                            const float influence=morph.shape->offset_weights.count ? float(morph.shape->offset_weights.data[offset]) : 1;
                            if(!std::isfinite(influence)) throw std::runtime_error("FBX morph influence must be finite");
                            target.positions.back()=checkedVector(ufbx_transform_direction(&node->geometry_to_world,morph.shape->position_offsets.data[offset]))*influence;
                            if(!target.normals.empty()) {
                                const auto original=ufbx_get_vertex_vec3(&source->vertex_normal,index),delta=morph.shape->normal_offsets.data[offset];
                                ufbx_vec3 changed{};changed.x=original.x+delta.x;changed.y=original.y+delta.y;changed.z=original.z+delta.z;
                                target.normals.back()=(checkedVector(ufbx_vec3_normalize(ufbx_transform_direction(&normalMatrix,changed)))-n)*influence;
                            }
                        }
                        if(skin.active()) result->skin.push_back(skin.vertex(*node,index));
                    }
                    output[corner]=mapped;
                }
                // Negative node scale reverses handedness after parser coordinate conversion.
                if(determinant<0) std::swap(output[1],output[2]);
                materialIndices[material].insert(materialIndices[material].end(),std::begin(output),std::end(output));
                totalIndices+=3;
            }
        }
    }
    for(std::size_t i=0;i<materialIndices.size();++i) {
        const auto& indices=materialIndices[i]; if(indices.empty()) continue;
        result->parts.push_back({static_cast<std::uint32_t>(result->indices.size()),static_cast<std::uint32_t>(indices.size()),static_cast<std::uint32_t>(i)});
        result->indices.insert(result->indices.end(),indices.begin(),indices.end());
    }
    if(result->indices.empty()) throw std::runtime_error("FBX contains no triangle meshes");
    skin.finish();
    result->generateTangents();
    checkMorphBudget(result->vertices.size());
    // Rebuild each target's tangent basis after final seam splitting. UVs and
    // topology stay fixed, so target offsets retain the base vertex order.
    for(auto& target:result->morphTargets) {
        Mesh shape;shape.vertices=result->vertices;shape.indices=result->indices;
        for(std::size_t i=0;i<shape.vertices.size();++i) {
            shape.vertices[i].position=shape.vertices[i].position+target.positions[i];
            if(!target.normals.empty()) shape.vertices[i].normal=normalized(shape.vertices[i].normal+target.normals[i]);
        }
        shape.generateTangents();
        if(shape.vertices.size()!=result->vertices.size()) throw std::runtime_error("FBX morph tangent topology differs from base mesh");
        target.tangents.resize(shape.vertices.size());
        for(std::size_t i=0;i<shape.vertices.size();++i) {
            const auto& a=shape.vertices[i].tangent;const auto& b=result->vertices[i].tangent;
            target.tangents[i]={a[0]-b[0],a[1]-b[1],a[2]-b[2]};
        }
    }
    if(result->vertices.size()>options.maxVertices) throw std::runtime_error("Tangent seams exceed vertex limit");
    if(options.measurements) options.measurements->convertMs=std::max(0.0,elapsedMs(convertStart)-textureDecodeMs);
    const auto inputVertices=result->vertices.size();
    if(options.optimizeMeshes) {
        const auto optimizeStart=ImportClock::now();
        MeshOptimizer::optimize(*result);
        if(options.measurements) options.measurements->optimizeMs=elapsedMs(optimizeStart);
    }
    if(options.measurements) {
        options.measurements->inputVertices=inputVertices;
        options.measurements->outputVertices=result->vertices.size();
    }
    return result;
}
#ifndef __EMSCRIPTEN__
std::shared_ptr<const Mesh> FbxImporter::loadFile(const std::filesystem::path& path,const FbxImportOptions& options) {
    validateOptions(options);
    std::ifstream file(path,std::ios::binary|std::ios::ate);
    if(!file) throw std::runtime_error("Cannot open FBX file: "+path.string());
    const auto length=file.tellg();
    if(length<0 || static_cast<std::uintmax_t>(length)>options.maxFileBytes)
        throw std::runtime_error("FBX file exceeds the size limit or cannot be read");
    std::vector<std::byte> data(static_cast<std::size_t>(length));
    file.seekg(0);
    if(!file.read(reinterpret_cast<char*>(data.data()),static_cast<std::streamsize>(data.size())))
        throw std::runtime_error("Cannot read FBX file: "+path.string());
    auto configured=options;
    if(!configured.resolveTexture) {
        const auto root=std::filesystem::weakly_canonical(path.parent_path().empty() ? std::filesystem::path(".") : path.parent_path());
        configured.resolveTexture=[root,limit=options.textureLimits.maxFileBytes](std::string_view name) {
            const std::filesystem::path relative{std::string(name)};
            std::vector<std::filesystem::path> candidates;
            if(!relative.is_absolute() && !relative.has_root_name()) candidates.push_back(root/relative);
            candidates.push_back(root/relative.filename()); // Relocated exports often keep only the basename.
            for(const auto& candidate:candidates) {
                const auto resolved=std::filesystem::weakly_canonical(candidate);
                auto base=root.begin(),entry=resolved.begin();
                for(;base!=root.end() && entry!=resolved.end() && *base==*entry;++base,++entry) {}
                if(base!=root.end()) continue; // Includes symlinks and ../ escaping the model directory.
                std::ifstream image(resolved,std::ios::binary|std::ios::ate); if(!image) continue;
                const auto size=image.tellg();
                if(size<0 || static_cast<std::uintmax_t>(size)>limit) throw std::runtime_error("External texture exceeds file size limit");
                std::vector<std::byte> bytes(static_cast<std::size_t>(size)); image.seekg(0);
                if(!image.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()))) throw std::runtime_error("Cannot read external texture");
                return bytes;
            }
            return std::vector<std::byte>{};
        };
    }
    return loadMemory(data,configured);
}
#endif
} // namespace engine
