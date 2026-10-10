#include "engine/glb_importer.hpp"
#include "engine/mesh_optimizer.hpp"
#include <cgltf.h>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#ifndef __EMSCRIPTEN__
#include <fstream>
#endif

namespace engine {
namespace {
using Clock=std::chrono::steady_clock;
double elapsed(Clock::time_point t) { return std::chrono::duration<double,std::milli>(Clock::now()-t).count(); }
void require(bool ok,const char* message) { if(!ok) throw std::runtime_error(std::string("GLB: ")+message); }
float finite(float f) { require(std::isfinite(f),"non-finite attribute");return f; }
Vec3 vec(const float* p) { return {finite(p[0]),finite(p[1]),finite(p[2])}; }
struct Budget {
    std::size_t used=0,limit;
    struct alignas(std::max_align_t) Header { std::size_t size; };
    static void* alloc(void* user,std::size_t size) {
        auto& b=*static_cast<Budget*>(user);
        if(size>b.limit-b.used || size>SIZE_MAX-sizeof(Header)) return nullptr;
        auto* p=static_cast<Header*>(std::malloc(sizeof(Header)+size));if(!p) return nullptr;
        p->size=size;b.used+=size;return p+1;
    }
    static void free(void* user,void* ptr) {
        if(!ptr) return;
        auto* p=static_cast<Header*>(ptr)-1;static_cast<Budget*>(user)->used-=p->size;std::free(p);
    }
};
void validate(const GlbImportOptions& o) {
    require(o.maxFileBytes&&o.maxVertices&&o.maxIndices&&o.parserMemoryBytes&&o.maxTextureBytes&&o.maxAnimationBytes&&o.maxMorphBytes&&o.maxMorphTargets,
        "invalid resource limits");
    require(o.maxVertices<=UINT32_MAX&&o.maxIndices<=UINT32_MAX&&std::isfinite(o.animationSampleRate)&&o.animationSampleRate>=1&&o.animationSampleRate<=120,
        "invalid geometry limits or sample rate");
    require(o.textureLimits.maxFileBytes&&o.textureLimits.maxDimension&&o.textureLimits.maxDecodedBytes,"invalid texture limits");
}
std::vector<float> unpack(const cgltf_accessor* a,cgltf_type type,std::size_t limit) {
    require(a&&a->type==type,"missing or incorrectly typed accessor");
    const auto width=cgltf_num_components(type);
    require(a->count<=limit/sizeof(float)/width,"accessor exceeds decoded memory limit");
    std::vector<float> out(a->count*width);
    require(cgltf_accessor_unpack_floats(a,out.data(),out.size())==out.size(),"cannot read accessor");
    for(auto f:out) finite(f);
    return out;
}
const cgltf_accessor* attribute(const cgltf_attribute* attrs,std::size_t count,cgltf_attribute_type type,int index=0) {
    for(std::size_t i=0;i<count;++i) if(attrs[i].type==type&&attrs[i].index==index) return attrs[i].data;
    return nullptr;
}
JointPose localPose(const cgltf_node& n) {
    if(!n.has_matrix) return {vec(n.translation),vec(n.scale),{n.rotation[0],n.rotation[1],n.rotation[2],n.rotation[3]}};
    Mat4 m;std::copy_n(n.matrix,16,m.m.begin());
    JointPose p;p.translation={m.m[12],m.m[13],m.m[14]};
    Vec3 axes[3]={{m.m[0],m.m[1],m.m[2]},{m.m[4],m.m[5],m.m[6]},{m.m[8],m.m[9],m.m[10]}};
    p.scale={std::sqrt(dot(axes[0],axes[0])),std::sqrt(dot(axes[1],axes[1])),std::sqrt(dot(axes[2],axes[2]))};
    if(dot(axes[0],cross(axes[1],axes[2]))<0) p.scale.x=-p.scale.x;
    require(std::abs(p.scale.x)>1e-8f&&p.scale.y>1e-8f&&p.scale.z>1e-8f,"singular node matrix");
    axes[0]=axes[0]*(1/p.scale.x);axes[1]=axes[1]*(1/p.scale.y);axes[2]=axes[2]*(1/p.scale.z);
    float r[3][3]={{axes[0].x,axes[1].x,axes[2].x},{axes[0].y,axes[1].y,axes[2].y},{axes[0].z,axes[1].z,axes[2].z}};
    float q[4]{};const float trace=r[0][0]+r[1][1]+r[2][2];
    if(trace>0) { const float s=std::sqrt(trace+1)*2;q[3]=s/4;q[0]=(r[2][1]-r[1][2])/s;q[1]=(r[0][2]-r[2][0])/s;q[2]=(r[1][0]-r[0][1])/s; }
    else { int i=0;if(r[1][1]>r[i][i]) i=1;if(r[2][2]>r[i][i]) i=2;int j=(i+1)%3,k=(i+2)%3;
        const float s=std::sqrt(1+r[i][i]-r[j][j]-r[k][k])*2;q[i]=s/4;q[j]=(r[i][j]+r[j][i])/s;q[k]=(r[i][k]+r[k][i])/s;q[3]=(r[k][j]-r[j][k])/s; }
    p.rotation={q[0],q[1],q[2],q[3]};const auto rebuilt=p.matrix();
    for(int i=0;i<16;++i) require(std::abs(rebuilt.m[i]-finite(m.m[i]))<0.002f*std::max(1.f,std::abs(m.m[i])),"unsupported sheared node matrix");
    return p;
}
std::vector<std::byte> dataUri(std::string_view uri,std::size_t limit) {
    const auto comma=uri.find(',');require(comma!=uri.npos&&uri.substr(0,comma).ends_with(";base64"),"unsupported image data URI");
    auto s=uri.substr(comma+1);require(s.size()%4==0&&s.size()/4<=limit/3+1,"invalid or oversized base64 image");
    std::vector<std::byte> out;out.reserve(std::min(limit,s.size()/4*3));
    unsigned value=0,bits=0;bool padding=false;
    for(char c:s) {
        if(c=='=') { padding=true;continue; }
        require(!padding,"invalid base64 padding");
        const auto index=std::string_view("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/").find(c);
        require(index!=std::string_view::npos,"invalid base64 image");value=(value<<6)|unsigned(index);bits+=6;
        if(bits>=8) { bits-=8;require(out.size()<limit,"image exceeds file limit");out.push_back(std::byte((value>>bits)&255)); }
    }
    return out;
}
struct UvMapping {
    int set=0;Vec2 offset{},scale{1,1};float rotation=0;
    bool operator==(const UvMapping& b) const {return set==b.set&&offset.x==b.offset.x&&offset.y==b.offset.y&&scale.x==b.scale.x&&scale.y==b.scale.y&&rotation==b.rotation;}
};
UvMapping mapping(const cgltf_texture_view& view) {
    UvMapping m;m.set=view.texcoord;
    if(view.has_transform) {const auto& t=view.transform;m.offset={finite(t.offset[0]),finite(t.offset[1])};m.scale={finite(t.scale[0]),finite(t.scale[1])};m.rotation=finite(t.rotation);if(t.has_texcoord)m.set=t.texcoord;}
    require(m.set>=0,"invalid texture coordinate set");return m;
}
struct Import {
    const GlbImportOptions& o;cgltf_data& d;Mesh& mesh;
    std::shared_ptr<Skeleton> skeleton;
    std::unordered_map<const cgltf_node*,std::uint32_t> joints;
    std::unordered_map<const cgltf_texture*,std::shared_ptr<const Texture>> textures;
    std::unordered_map<const cgltf_material*,std::uint32_t> materials;
    std::vector<UvMapping> materialUvs;
    std::size_t textureBytes=0;double decodeMs=0;
    Import(const GlbImportOptions& options,cgltf_data& data,Mesh& result):o(options),d(data),mesh(result) {}
    void warn(std::string message) { if(std::find(mesh.warnings.begin(),mesh.warnings.end(),message)==mesh.warnings.end()) mesh.warnings.push_back(std::move(message)); }
    std::uint32_t joint(const cgltf_node* n) {
        if(auto it=joints.find(n);it!=joints.end()) return it->second;
        const auto parent=n->parent ? std::int32_t(joint(n->parent)) : -1;
        require(joints.size()<256,"skeleton exceeds 256 hierarchy joints");const auto id=std::uint32_t(joints.size());
        joints[n]=id;skeleton->joints.push_back({n->name?n->name:"Node",parent,localPose(*n)});return id;
    }
    std::uint32_t binding(const cgltf_node* n,const Mat4& inverse) {
        require(skeleton->bindings.size()<1024,"exceeds 1024 skin bindings");
        auto id=std::uint32_t(skeleton->bindings.size());skeleton->bindings.push_back({joint(n),inverse});return id;
    }
    std::shared_ptr<const Texture> texture(const cgltf_texture_view& view,const UvMapping& primary) {
        if(!view.texture) return {};
        if(!(mapping(view)==primary)) {warn("Texture uses a different UV set or transform from the material primary map; texture omitted");return {};}
        if(auto it=textures.find(view.texture);it!=textures.end()) return it->second;
        const auto* image=view.texture->image;
        if(!image) { warn("Texture has no supported PNG/JPEG source");return {}; }
        std::vector<std::byte> owned;std::span<const std::byte> bytes;
        if(image->buffer_view) { auto* v=image->buffer_view;bytes={reinterpret_cast<const std::byte*>(cgltf_buffer_view_data(v)),v->size}; }
        else if(image->uri) {
            const std::string_view uri(image->uri);
            if(uri.starts_with("data:")) owned=dataUri(uri,o.textureLimits.maxFileBytes);
            else if(o.resolveTexture) {std::string decoded(uri);cgltf_decode_uri(decoded.data());decoded.resize(std::strlen(decoded.c_str()));owned=o.resolveTexture(decoded);}
            bytes=owned;
        }
        if(bytes.empty()) { warn(std::string("Missing companion texture: ")+(image->uri?image->uri:"unnamed"));return {}; }
        require(textureBytes<o.maxTextureBytes,"textures exceed total decoded memory limit");
        auto limits=o.textureLimits;limits.maxDecodedBytes=std::min(limits.maxDecodedBytes,o.maxTextureBytes-textureBytes);
        const auto start=Clock::now();auto decoded=Texture::loadMemory(bytes,image->name?image->name:"GLB texture",limits);
        auto result=std::make_shared<Texture>(*decoded);
        if(const auto* s=view.texture->sampler) {
            result->wrapU=s->wrap_s==cgltf_wrap_mode_clamp_to_edge?TextureWrap::Clamp:TextureWrap::Repeat;
            result->wrapV=s->wrap_t==cgltf_wrap_mode_clamp_to_edge?TextureWrap::Clamp:TextureWrap::Repeat;
            if(s->wrap_s==cgltf_wrap_mode_mirrored_repeat||s->wrap_t==cgltf_wrap_mode_mirrored_repeat) warn("Mirrored texture wrapping approximated with repeat");
        }
        decodeMs+=elapsed(start);textureBytes+=result->pixels.size();textures[view.texture]=result;return result;
    }
    std::uint32_t material(const cgltf_material* m) {
        if(auto it=materials.find(m);it!=materials.end()) return it->second;
        auto result=std::make_shared<Material>();result->roughness=1;result->metallic=1;UvMapping primary;
        if(m) {
            result->name=m->name?m->name:"GLB material";const auto& p=m->pbr_metallic_roughness;
            for(const auto* view:{&p.base_color_texture,&p.metallic_roughness_texture,&m->normal_texture,&m->occlusion_texture,&m->emissive_texture}) if(view->texture) {primary=mapping(*view);break;}
            result->baseColor=vec(p.base_color_factor);result->opacity=p.base_color_factor[3];result->roughness=p.roughness_factor;result->metallic=p.metallic_factor;
            result->baseColorTexture=texture(p.base_color_texture,primary);result->metallicRoughnessTexture=texture(p.metallic_roughness_texture,primary);
            result->normalTexture=texture(m->normal_texture,primary);result->normalScale=m->normal_texture.scale;
            result->occlusionTexture=texture(m->occlusion_texture,primary);result->occlusionStrength=m->occlusion_texture.scale;
            result->emissive=vec(m->emissive_factor)*(m->has_emissive_strength?m->emissive_strength.emissive_strength:1);
            result->emissiveTexture=texture(m->emissive_texture,primary);result->doubleSided=m->double_sided;
            result->alphaMode=m->alpha_mode==cgltf_alpha_mode_blend?AlphaMode::Blend:m->alpha_mode==cgltf_alpha_mode_mask?AlphaMode::Mask:AlphaMode::Opaque;
            result->alphaCutoff=m->alpha_cutoff;result->shader=m->unlit?MaterialShader::Unlit:MaterialShader::Pbr;
            if(m->has_clearcoat||m->has_transmission||m->has_volume||m->has_specular||m->has_sheen||m->has_pbr_specular_glossiness||m->has_iridescence||m->has_anisotropy||m->has_diffuse_transmission||m->has_dispersion||m->has_ior)
                warn("Optional advanced material extensions approximated by metallic-roughness PBR");
        }
        result->validate();auto id=std::uint32_t(mesh.materials.size());materials[m]=id;mesh.materials.push_back(result);materialUvs.push_back(primary);return id;
    }
    void geometry(const cgltf_node& node) {
        if(!node.mesh) return;
        require(!node.has_mesh_gpu_instancing,"EXT_mesh_gpu_instancing is unsupported");
        Mat4 world;cgltf_node_transform_world(&node,world.m.data());for(auto f:world.m) finite(f);
        const auto inv=inverseAffine(world),normal=transpose(inv);
        const bool mirrored=dot(Vec3{world.m[0],world.m[1],world.m[2]},cross(Vec3{world.m[4],world.m[5],world.m[6]},Vec3{world.m[8],world.m[9],world.m[10]}))<0;
        std::vector<std::uint32_t> skinBindings;std::uint32_t rigid=0;
        if(skeleton) {
            rigid=binding(&node,inv);
            if(node.skin) {
                const auto* skin=node.skin;std::vector<float> ib;
                if(skin->inverse_bind_matrices) {require(skin->inverse_bind_matrices->count==skin->joints_count,"inverse bind count mismatch");ib=unpack(skin->inverse_bind_matrices,cgltf_type_mat4,o.parserMemoryBytes);}
                for(std::size_t j=0;j<skin->joints_count;++j) {
                    auto matrix=Mat4::identity();if(!ib.empty()) std::copy_n(ib.data()+j*16,16,matrix.m.begin());
                    skinBindings.push_back(binding(skin->joints[j],matrix*inv));
                }
            }
        }
        const auto morphBase=mesh.morphTargets.size();
        const auto targetCount=node.mesh->primitives_count?node.mesh->primitives[0].targets_count:0;
        require(targetCount<=o.maxMorphTargets-morphBase,"morph target count exceeds limit");
        require(morphBase+targetCount==0||mesh.vertices.size()<=o.maxMorphBytes/sizeof(Vec3)/3/(morphBase+targetCount),"morph memory limit exceeded");
        for(std::size_t t=0;t<targetCount;++t) {
            MorphTarget target;target.name=std::string(node.name?node.name:"Mesh")+"/"+(t<node.mesh->target_names_count?node.mesh->target_names[t]:std::to_string(t));
            for(auto& existing:mesh.morphTargets) if(existing.name==target.name) target.name+="/"+std::to_string(morphBase+t);
            target.defaultWeight=finite(t<node.weights_count?node.weights[t]:t<node.mesh->weights_count?node.mesh->weights[t]:0);
            target.positions.resize(mesh.vertices.size());target.normals.resize(mesh.vertices.size());mesh.morphTargets.push_back(std::move(target));
        }
        for(std::size_t pi=0;pi<node.mesh->primitives_count;++pi) {
            const auto& p=node.mesh->primitives[pi];require(!p.has_draco_mesh_compression,"Draco compression is unsupported");
            require(p.targets_count==targetCount,"inconsistent morph target counts");
            if(p.type!=cgltf_primitive_type_triangles&&p.type!=cgltf_primitive_type_triangle_strip&&p.type!=cgltf_primitive_type_triangle_fan) {warn("Non-triangle primitive omitted");continue;}
            auto attr=[&](cgltf_attribute_type type,int index=0){return attribute(p.attributes,p.attributes_count,type,index);};
            auto* positions=attr(cgltf_attribute_type_position);require(positions,"primitive has no positions");const auto count=positions->count;
            require(count<=o.maxVertices-mesh.vertices.size(),"vertex count exceeds limit");
            require(!p.indices||!p.indices->is_sparse,"sparse index accessors are unsupported");
            const auto sourceCount=p.indices?p.indices->count:count;
            require(sourceCount>=3,"primitive has fewer than three indices");
            const auto triangleCount=p.type==cgltf_primitive_type_triangles?sourceCount/3:sourceCount-2;
            require(p.type!=cgltf_primitive_type_triangles||sourceCount%3==0,"invalid triangle index count");
            require(triangleCount<=(o.maxIndices-mesh.indices.size())/3,"index count exceeds limit");
            require(mesh.morphTargets.empty()||(mesh.vertices.size()+count)<=o.maxMorphBytes/sizeof(Vec3)/3/mesh.morphTargets.size(),"morph memory limit exceeded");
            auto stream=[&](const cgltf_accessor* a,cgltf_type type){if(!a)return std::vector<float>{};require(a->count==count,"vertex attribute counts differ");return unpack(a,type,o.parserMemoryBytes);};
            const auto mat=material(p.material);const auto& mapping=materialUvs[mat];
            const auto pos=stream(positions,cgltf_type_vec3),norm=stream(attr(cgltf_attribute_type_normal),cgltf_type_vec3),uv=stream(attr(cgltf_attribute_type_texcoord,mapping.set),cgltf_type_vec2);
            const auto& materialAsset=*mesh.materials[mat];
            if(uv.empty()&&(materialAsset.baseColorTexture||materialAsset.metallicRoughnessTexture||materialAsset.normalTexture||materialAsset.occlusionTexture||materialAsset.emissiveTexture)) warn("Primitive is missing the material texture coordinate set; using (0,0)");
            const auto js=stream(attr(cgltf_attribute_type_joints),cgltf_type_vec4),ws=stream(attr(cgltf_attribute_type_weights),cgltf_type_vec4);
            const auto js1=stream(attr(cgltf_attribute_type_joints,1),cgltf_type_vec4),ws1=stream(attr(cgltf_attribute_type_weights,1),cgltf_type_vec4);
            for(std::size_t a=0;a<p.attributes_count;++a) if(p.attributes[a].type==cgltf_attribute_type_joints&&p.attributes[a].index>1) warn("Skin influence sets beyond JOINTS_1 are unsupported and omitted");
            require(js.empty()==ws.empty()&&js1.empty()==ws1.empty(),"joints/weights streams must be paired");
            require(!node.skin||!js.empty(),"skinned primitive is missing weights");
            const auto base=std::uint32_t(mesh.vertices.size()),first=std::uint32_t(mesh.indices.size());
            for(std::size_t v=0;v<count;++v) {
                const auto position=transformPoint(world,vec(pos.data()+v*3));finite(position.x);finite(position.y);finite(position.z);
                auto n=norm.empty()?Vec3{}:normalized(transformDirection(normal,vec(norm.data()+v*3)));
                if(!norm.empty()) require(dot(n,n)>0.5f,"degenerate vertex normal");
                Vec2 texcoord;
                if(!uv.empty()) {const float u=uv[v*2]*mapping.scale.x,w=uv[v*2+1]*mapping.scale.y,c=std::cos(mapping.rotation),s=std::sin(mapping.rotation);texcoord={finite(mapping.offset.x+c*u-s*w),finite(mapping.offset.y+s*u+c*w)};}
                mesh.vertices.push_back({position,n,texcoord});
                if(skeleton) {
                    SkinVertex sk;sk.joints[0]=rigid;
                    if(node.skin) {
                        std::vector<std::pair<float,std::uint32_t>> influences;
                        auto add=[&](const std::vector<float>& j,const std::vector<float>& w) { if(j.empty()) return;for(int k=0;k<4;++k) {
                            const auto index=j[v*4+k],weight=w[v*4+k];require(index>=0&&std::floor(index)==index&&index<skinBindings.size()&&weight>=0,"invalid skin influence");
                            if(weight>0) influences.emplace_back(weight,skinBindings[std::size_t(index)]);
                        }};add(js,ws);add(js1,ws1);
                        std::stable_sort(influences.begin(),influences.end(),[](auto a,auto b){return a.first>b.first;});
                        if(influences.size()>4) { influences.resize(4);warn("Skin influences reduced to the four strongest weights and normalized"); }
                        double sum=0;for(auto [w,j]:influences) sum+=w;
                        if(sum>0) {sk.weights={};for(std::size_t k=0;k<influences.size();++k){sk.weights[k]=float(influences[k].first/sum);sk.joints[k]=influences[k].second;}}
                    }
                    mesh.skin.push_back(sk);
                }
            }
            auto index=[&](std::size_t i) {const auto value=p.indices?cgltf_accessor_read_index(p.indices,i):i;require(value<count,"index outside vertex array");return base+std::uint32_t(value);};
            for(std::size_t t=0;t<triangleCount;++t) {
                std::uint32_t a,b,c;
                if(p.type==cgltf_primitive_type_triangles){a=index(t*3);b=index(t*3+1);c=index(t*3+2);}
                else if(p.type==cgltf_primitive_type_triangle_fan){a=index(0);b=index(t+1);c=index(t+2);}
                else {a=index(t);b=index(t+1);c=index(t+2);if(t%2) std::swap(a,b);}
                if(mirrored) std::swap(b,c);
                mesh.indices.insert(mesh.indices.end(),{a,b,c});
                if(norm.empty()) {const auto n=cross(mesh.vertices[b].position-mesh.vertices[a].position,mesh.vertices[c].position-mesh.vertices[a].position);for(auto i:{a,b,c}) mesh.vertices[i].normal=mesh.vertices[i].normal+n;}
            }
            if(norm.empty()) for(std::size_t v=base;v<mesh.vertices.size();++v) {auto& n=mesh.vertices[v].normal;n=normalized(n);if(dot(n,n)<0.5f)n={0,1,0};}
            mesh.parts.push_back({first,std::uint32_t(mesh.indices.size()-first),mat});
            for(auto& target:mesh.morphTargets) {target.positions.resize(mesh.vertices.size());target.normals.resize(mesh.vertices.size());}
            for(std::size_t t=0;t<targetCount;++t) {
                const auto& source=p.targets[t];auto& target=mesh.morphTargets[morphBase+t];
                const auto dp=stream(attribute(source.attributes,source.attributes_count,cgltf_attribute_type_position),cgltf_type_vec3);
                const auto dn=stream(attribute(source.attributes,source.attributes_count,cgltf_attribute_type_normal),cgltf_type_vec3);
                for(std::size_t v=0;v<count;++v) {
                    if(!dp.empty()) target.positions[base+v]=transformDirection(world,vec(dp.data()+v*3));
                    if(!dn.empty()&&!norm.empty()) target.normals[base+v]=normalized(transformDirection(normal,vec(norm.data()+v*3)+vec(dn.data()+v*3)))-mesh.vertices[base+v].normal;
                }
            }
            if(attr(cgltf_attribute_type_color)) warn("Vertex colors are unsupported by the material vertex format");
        }
    }
    void animations() {
        if(!skeleton) return;
        require(d.animations_count<=64,"exceeds 64 animation clips");std::size_t bytes=0;
        for(std::size_t ai=0;ai<d.animations_count;++ai) {
            const auto& a=d.animations[ai];
            struct Channel {std::uint32_t joint;cgltf_animation_path_type path;cgltf_interpolation_type interpolation;std::vector<float> times,values;std::size_t width;};
            std::vector<Channel> channels;double end=0;std::size_t decoded=0;
            std::unordered_set<std::uint64_t> targets;
            for(std::size_t ci=0;ci<a.channels_count;++ci) {
                const auto& c=a.channels[ci];require(c.target_node&&c.sampler,"invalid animation channel");
                if(c.target_path==cgltf_animation_path_type_weights) {
                    require(c.sampler->input&&c.sampler->input->type==cgltf_type_scalar&&c.sampler->output&&c.sampler->output->type==cgltf_type_scalar,
                        "morph animation timestamps and weights must use scalar accessors");
                    warn("Animated morph weights are unsupported; default morph weights retained");continue;
                }
                auto it=joints.find(c.target_node);if(it==joints.end()) continue;
                require(!c.target_node->has_matrix,"animation targets a matrix node");
                require(targets.insert(std::uint64_t(it->second)*8+c.target_path).second,"duplicate animation channel");
                require(c.target_path==cgltf_animation_path_type_translation||c.target_path==cgltf_animation_path_type_rotation||c.target_path==cgltf_animation_path_type_scale,"unsupported animation path");
                const auto& s=*c.sampler;const std::size_t width=c.target_path==cgltf_animation_path_type_rotation?4:3;
                require(s.interpolation==cgltf_interpolation_type_linear||s.interpolation==cgltf_interpolation_type_step||s.interpolation==cgltf_interpolation_type_cubic_spline,"unsupported interpolation");
                auto times=unpack(s.input,cgltf_type_scalar,o.maxAnimationBytes-decoded);decoded+=times.size()*sizeof(float);
                auto values=unpack(s.output,width==4?cgltf_type_vec4:cgltf_type_vec3,o.maxAnimationBytes-decoded);decoded+=values.size()*sizeof(float);
                require(!times.empty()&&times.front()>=0,"invalid animation timestamps");
                for(std::size_t i=1;i<times.size();++i) require(times[i]>times[i-1],"animation timestamps must increase");
                require(values.size()/width/(s.interpolation==cgltf_interpolation_type_cubic_spline?3:1)==times.size()&&values.size()==times.size()*width*(s.interpolation==cgltf_interpolation_type_cubic_spline?3:1),"animation output count mismatch");
                end=std::max(end,double(times.back()));channels.push_back({it->second,c.target_path,s.interpolation,std::move(times),std::move(values),width});
            }
            if(channels.empty()) continue;
            const double frameCount=std::max(1.0,std::ceil(end*o.animationSampleRate)+1);const auto frameBytes=skeleton->joints.size()*sizeof(JointPose);
            require(frameCount<=UINT32_MAX&&frameCount<=double((o.maxAnimationBytes-bytes)/frameBytes),"sampled animation exceeds memory limit");
            AnimationClip clip{a.name?a.name:"Animation",end,std::uint32_t(frameCount),{}};
            clip.frames.reserve(clip.frameCount*skeleton->joints.size());bytes+=clip.frameCount*frameBytes;
            for(std::uint32_t frame=0;frame<clip.frameCount;++frame) {
                const auto offset=clip.frames.size();for(auto& j:skeleton->joints) clip.frames.push_back(j.bindPose);
                const double time=clip.frameCount>1?end*frame/(clip.frameCount-1):0;
                for(const auto& c:channels) {
                    auto upper=std::upper_bound(c.times.begin(),c.times.end(),float(time));
                    const auto left=upper==c.times.begin()?0:std::size_t(upper-c.times.begin()-1),right=std::min(left+1,c.times.size()-1);
                    const float dt=c.times[right]-c.times[left],u=dt>0?std::clamp(float((time-c.times[left])/dt),0.f,1.f):0;
                    const bool cubic=c.interpolation==cgltf_interpolation_type_cubic_spline;const auto stride=c.width*(cubic?3:1);
                    const float* av=c.values.data()+left*stride+(cubic?c.width:0);const float* bv=c.values.data()+right*stride+(cubic?c.width:0);
                    float v[4]{};
                    for(std::size_t k=0;k<c.width;++k) {
                        if(c.interpolation==cgltf_interpolation_type_step) v[k]=av[k];
                        else if(cubic) v[k]=(2*u*u*u-3*u*u+1)*av[k]+(u*u*u-2*u*u+u)*dt*av[c.width+k]+(-2*u*u*u+3*u*u)*bv[k]+(u*u*u-u*u)*dt*(bv-c.width)[k];
                        else v[k]=av[k]*(1-u)+bv[k]*u;
                    }
                    auto& pose=clip.frames[offset+c.joint];
                    if(c.path==cgltf_animation_path_type_translation) pose.translation=vec(v);
                    else if(c.path==cgltf_animation_path_type_scale) pose.scale=vec(v);
                    else if(c.interpolation==cgltf_interpolation_type_linear) {JointPose pa,pb;pa.rotation={av[0],av[1],av[2],av[3]};pb.rotation={bv[0],bv[1],bv[2],bv[3]};pose.rotation=blendPose(pa,pb,u).rotation;}
                    else {const auto length=std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]+v[3]*v[3]);require(length>1e-8f,"invalid animated quaternion");pose.rotation={v[0]/length,v[1]/length,v[2]/length,v[3]/length};}
                }
            }
            skeleton->clips.push_back(std::move(clip));
        }
        skeleton->validate();mesh.skeleton=skeleton;
    }
};
}
std::shared_ptr<const Mesh> GlbImporter::loadMemory(std::span<const std::byte> bytes,const GlbImportOptions& o) {
    const auto start=Clock::now();if(o.measurements)*o.measurements={};
    struct Timer {GlbImportMeasurements* m;Clock::time_point start;~Timer(){if(m)m->totalMs=elapsed(start);}} timer{o.measurements,start};
    validate(o);require(!bytes.empty()&&bytes.size()<=o.maxFileBytes,"empty file or file size limit exceeded");
    Budget budget{0,o.parserMemoryBytes};cgltf_options opts{};opts.type=cgltf_file_type_glb;opts.memory={Budget::alloc,Budget::free,&budget};
    cgltf_data* raw=nullptr;const auto parsed=cgltf_parse(&opts,bytes.data(),bytes.size(),&raw);
    std::unique_ptr<cgltf_data,decltype(&cgltf_free)> data(raw,cgltf_free);require(parsed==cgltf_result_success,"invalid binary glTF 2.0 container or parser memory limit");
    auto& d=*data;
    for(std::size_t i=0;i<d.extensions_required_count;++i) {
        const std::string_view e=d.extensions_required[i];require(e=="KHR_materials_unlit"||e=="KHR_materials_emissive_strength"||e=="KHR_mesh_quantization"||e=="KHR_texture_transform","unsupported required glTF extension");
    }
    require(d.buffers_count==1&&!d.buffers[0].uri&&d.bin&&d.buffers[0].size<=d.bin_size,"GLB must contain one embedded geometry buffer");
    d.buffers[0].data=const_cast<void*>(d.bin);
    // Validate all byte ranges before cgltf's validator reads index data.
    for(std::size_t i=0;i<d.buffer_views_count;++i) {auto& v=d.buffer_views[i];require(!v.has_meshopt_compression,"meshopt compression is unsupported");require(v.buffer&&v.offset<=v.buffer->size&&v.size<=v.buffer->size-v.offset,"buffer view outside buffer");}
    for(std::size_t i=0;i<d.accessors_count;++i) {
        const auto& a=d.accessors[i];const auto size=cgltf_calc_size(a.type,a.component_type);
        require(a.count&&size&&a.stride>=size,"invalid accessor layout");
        if(a.buffer_view) require(a.offset<=a.buffer_view->size&&(a.count==0||(size<=a.buffer_view->size-a.offset&&(a.count-1)<=(a.buffer_view->size-a.offset-size)/a.stride)),"accessor outside buffer view");
        if(a.is_sparse) {
            const auto& s=a.sparse;const auto indexSize=cgltf_component_size(s.indices_component_type);
            require(indexSize&&s.count<=a.count&&s.indices_buffer_view&&s.values_buffer_view,"invalid sparse accessor");
            require(s.indices_byte_offset<=s.indices_buffer_view->size&&s.count<=(s.indices_buffer_view->size-s.indices_byte_offset)/indexSize&&s.values_byte_offset<=s.values_buffer_view->size&&s.count<=(s.values_buffer_view->size-s.values_byte_offset)/size,"sparse accessor outside buffer view");
        }
    }
    for(std::size_t i=0;i<d.nodes_count;++i) {auto* n=&d.nodes[i];std::size_t depth=0;for(auto* p=n;p;p=p->parent) require(++depth<=256,"cyclic or excessively deep node hierarchy");}
    require(cgltf_validate(&d)==cgltf_result_success,"invalid glTF data");
    if(o.measurements)o.measurements->parseMs=elapsed(start);
    const auto convert=Clock::now();auto result=std::make_shared<Mesh>();Import import{o,d,*result};
    const auto* scene=d.scene?d.scene:d.scenes_count?&d.scenes[0]:nullptr;
    std::vector<const cgltf_node*> nodes;std::unordered_set<const cgltf_node*> visited;
    std::function<void(const cgltf_node*)> visit=[&](const cgltf_node* n){require(visited.insert(n).second,"duplicate node in scene");nodes.push_back(n);for(std::size_t i=0;i<n->children_count;++i)visit(n->children[i]);};
    if(scene) {for(std::size_t i=0;i<scene->nodes_count;++i) visit(scene->nodes[i]);}
    else for(std::size_t i=0;i<d.nodes_count;++i) if(!d.nodes[i].parent) visit(&d.nodes[i]);
    const bool animated=d.animations_count||std::any_of(nodes.begin(),nodes.end(),[](auto* n){return n->skin!=nullptr;});
    if(animated) {import.skeleton=std::make_shared<Skeleton>();import.skeleton->bindings.push_back({});}
    for(auto* n:nodes) import.geometry(*n);
    require(!result->indices.empty(),"no triangle meshes in selected scene");import.animations();
    result->generateTangents();require(result->vertices.size()<=o.maxVertices,"tangent seams exceed vertex limit");
    require(result->morphTargets.empty()||result->vertices.size()<=o.maxMorphBytes/sizeof(Vec3)/3/result->morphTargets.size(),"morph memory limit exceeded");
    for(auto& target:result->morphTargets) {
        Mesh shape;shape.vertices=result->vertices;shape.indices=result->indices;
        for(std::size_t i=0;i<shape.vertices.size();++i) {shape.vertices[i].position=shape.vertices[i].position+target.positions[i];shape.vertices[i].normal=normalized(shape.vertices[i].normal+target.normals[i]);}
        shape.generateTangents();require(shape.vertices.size()==result->vertices.size(),"morph tangent topology differs from base");target.tangents.resize(shape.vertices.size());
        for(std::size_t i=0;i<shape.vertices.size();++i) {const auto& a=shape.vertices[i].tangent;const auto& b=result->vertices[i].tangent;target.tangents[i]={a[0]-b[0],a[1]-b[1],a[2]-b[2]};}
    }
    result->validateMorphTargets();
    if(o.measurements) {o.measurements->convertMs=std::max(0.0,elapsed(convert)-import.decodeMs);o.measurements->textureDecodeMs=import.decodeMs;o.measurements->decodedTextureBytes=import.textureBytes;o.measurements->decodedTextures=import.textures.size();o.measurements->inputVertices=result->vertices.size();}
    const auto optimize=Clock::now();if(o.optimizeMeshes) MeshOptimizer::optimize(*result);
    if(o.measurements) {o.measurements->optimizeMs=o.optimizeMeshes?elapsed(optimize):0;o.measurements->outputVertices=result->vertices.size();}
    return result;
}
#ifndef __EMSCRIPTEN__
std::shared_ptr<const Mesh> GlbImporter::loadFile(const std::filesystem::path& path,const GlbImportOptions& options) {
    validate(options);std::ifstream file(path,std::ios::binary|std::ios::ate);require(bool(file),"cannot open file");
    const auto size=file.tellg();require(size>=0&&std::uintmax_t(size)<=options.maxFileBytes,"file exceeds size limit");
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));file.seekg(0);require(bool(file.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size()))),"cannot read file");
    auto configured=options;
    if(!configured.resolveTexture) {
        const auto root=std::filesystem::weakly_canonical(path.parent_path().empty()?std::filesystem::path("."):path.parent_path());
        configured.resolveTexture=[root,limit=options.textureLimits.maxFileBytes](std::string_view uri) {
            std::filesystem::path relative{std::string(uri)};if(relative.is_absolute()||relative.has_root_name())return std::vector<std::byte>{};
            const auto candidate=std::filesystem::weakly_canonical(root/relative);auto r=root.begin(),c=candidate.begin();
            for(;r!=root.end()&&c!=candidate.end()&&*r==*c;++r,++c){}if(r!=root.end())return std::vector<std::byte>{};
            std::ifstream image(candidate,std::ios::binary|std::ios::ate);if(!image)return std::vector<std::byte>{};
            const auto length=image.tellg();require(length>=0&&std::uintmax_t(length)<=limit,"companion image exceeds size limit");
            std::vector<std::byte> result(static_cast<std::size_t>(length));image.seekg(0);require(bool(image.read(reinterpret_cast<char*>(result.data()),std::streamsize(result.size()))),"cannot read image");return result;
        };
    }
    return loadMemory(bytes,configured);
}
#endif
}
