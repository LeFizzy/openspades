/*
 Copyright (c) 2013 yvt
 
 This file is part of OpenSpades.
 
 OpenSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 
 OpenSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with OpenSpades.  If not, see <http://www.gnu.org/licenses/>.
 
 */

#include "GLMapRenderer.h"
#include "../Client/GameMap.h"
#include "GLProgram.h"
#include "GLProgramAttribute.h"
#include "GLProgramUniform.h"
#include "GLMapChunk.h"
#include "GLRenderer.h"
#include "GLProgram.h"
#include "GLImage.h"
#include "IGLDevice.h"
#include "../Core/Debug.h"
#include "GLMapShadowRenderer.h"
#include "GLShadowShader.h"
#include "../Core/Settings.h"
#include "GLDynamicLightShader.h"
#include "GLProfiler.h"

SPADES_SETTING(r_physicalLighting, "0");

namespace spades {
	namespace draw {
		GLMapRenderer::GLMapRenderer(client::GameMap *m, GLRenderer *r):
		gameMap(m), renderer(r) {
			SPADES_MARK_FUNCTION();
			
			device = renderer->GetGLDevice();
			
			numChunkWidth = gameMap->Width() / GLMapChunk::Size;
			numChunkHeight = gameMap->Height() / GLMapChunk::Size;
			numChunkDepth = gameMap->Depth() / GLMapChunk::Size;
			
			numChunks = numChunkWidth * numChunkHeight * numChunkDepth;
			
			chunks = new GLMapChunk *[numChunks];
			chunkInfos = new ChunkRenderInfo[numChunks];
			
			for(int i = 0; i < numChunks; i++)
				chunks[i] = new GLMapChunk(this, gameMap,
										   i / numChunkDepth / numChunkHeight,
										   (i / numChunkDepth) % numChunkHeight,
										   i % numChunkDepth);
			
			
			if(r_physicalSolidLighting)
				basicProgram = renderer->RegisterProgram("Shaders/BasicBlockPhys.program");
			else
				basicProgram = renderer->RegisterProgram("Shaders/BasicBlock.program");
			dlightProgram = renderer->RegisterProgram("Shaders/BasicBlockDynamicLit.program");
			aoImage = (GLImage *)renderer->RegisterImage("Gfx/AmbientOcclusion.tga");
			detailImage = (GLImage *)renderer->RegisterImage("Textures/detail.jpg");
			
			static const uint8_t squareVertices[] = {
				0,0, 1,0, 0,1,
				1,0, 1,1, 0,1
			};
			squareVertexBuffer = device->GenBuffer();
			device->BindBuffer(IGLDevice::ArrayBuffer, squareVertexBuffer);
			device->BufferData(IGLDevice::ArrayBuffer, sizeof(squareVertices),
							   squareVertices, IGLDevice::StaticDraw);
			device->BindBuffer(IGLDevice::ArrayBuffer, 0);
		}
		
		GLMapRenderer::~GLMapRenderer() {
			SPADES_MARK_FUNCTION();
			
			device->DeleteBuffer(squareVertexBuffer);
			for(int i = 0; i < numChunks; i++)
				delete chunks[i];
			delete[] chunks;
			delete[] chunkInfos;
			
		}
		void GLMapRenderer::GameMapChanged(int x, int y, int z, client::GameMap *map) {
			SPADES_MARK_FUNCTION_DEBUG();
			
			/*GetChunk(x >> GLMapChunk::SizeBits,
					 y >> GLMapChunk::SizeBits,
					 z >> GLMapChunk::SizeBits)->SetNeedsUpdate();*/
			//int fx = x & (GLMapChunk::Size - 1);
			//int fy = y & (GLMapChunk::Size - 1);
			int fz = z & (GLMapChunk::Size - 1);
			int sx = -1;
			int sy = -1;
			int sz = fz == 0 ? -1 : 0;
			int ex = 1;
			int ey = 1;
			int ez = fz == (GLMapChunk::Size - 1) ? 1 : 0;
			for(int cx = sx; cx <= ex; cx++)
				for(int cy = sy; cy <= ey; cy++)
					for(int cz = sz; cz <= ez; cz++){
						int xx = x + cx, yy = y + cy, zz = z + cz;
						xx >>= GLMapChunk::SizeBits;
						yy >>= GLMapChunk::SizeBits;
						zz >>= GLMapChunk::SizeBits;
						xx &= numChunkWidth - 1;
						yy &= numChunkHeight - 1;
						if(xx >= 0 && yy >= 0 && zz >= 0 &&
						   xx < numChunkWidth && yy < numChunkHeight &&
						   zz < numChunkDepth) {
							GetChunk(xx, yy, zz)->SetNeedsUpdate();
						}
					}
		}
		
		void GLMapRenderer::RealizeChunks(spades::Vector3 eye) {
			SPADES_MARK_FUNCTION();
			
			float cullDistance = 128.f;
			float releaseDistance = 160.f;
			for(int i = 0; i < numChunks; i++){
				float dist = chunks[i]->DistanceFromEye(eye);
				chunkInfos[i].distance = dist;
				if(dist < cullDistance)
					chunks[i]->SetRealized(true);
				else if(dist > releaseDistance)
					chunks[i]->SetRealized(false);
			}
		}
		
		void GLMapRenderer::Prerender() {
			SPADES_MARK_FUNCTION();
			//Vector3 eye = renderer->GetSceneDef().viewOrigin;
			
			// nothing to do now
		}
		
		void GLMapRenderer::RenderSunlightPass() {
			SPADES_MARK_FUNCTION();
			GLProfiler profiler(device, "Map");
			
			Vector3 eye = renderer->GetSceneDef().viewOrigin;
			
			device->ActiveTexture(0);
			aoImage->Bind(IGLDevice::Texture2D);
			device->TexParamater(IGLDevice::Texture2D,
								 IGLDevice::TextureMinFilter,
								 IGLDevice::Linear);
			
			device->ActiveTexture(1);
			detailImage->Bind(IGLDevice::Texture2D);
			
			
			device->Enable(IGLDevice::CullFace, true);
			device->Enable(IGLDevice::DepthTest, true);
			
			basicProgram->Use();
			
			static GLShadowShader shadowShader;
			shadowShader(renderer, basicProgram, 2);
			
			static GLProgramUniform fogDistance("fogDistance");
			fogDistance(basicProgram);
			fogDistance.SetValue(renderer->GetFogDistance());
			
			static GLProgramUniform viewSpaceLight("viewSpaceLight");
			viewSpaceLight(basicProgram);
			Vector3 vspLight = (renderer->GetViewMatrix() * MakeVector4(0, -1, -1, 0)).GetXYZ();
			viewSpaceLight.SetValue(vspLight.x, vspLight.y, vspLight.z);
			
			static GLProgramUniform fogColor("fogColor");
			fogColor(basicProgram);
			Vector3 fogCol = renderer->GetFogColorForSolidPass();
			fogCol *= fogCol; // linearize
			fogColor.SetValue(fogCol.x, fogCol.y, fogCol.z);
			
			static GLProgramUniform aoUniform("ambientOcclusionTexture");
			aoUniform(basicProgram);
			aoUniform.SetValue(0);
			
			static GLProgramUniform detailTextureUnif("detailTexture");
			detailTextureUnif(basicProgram);
			detailTextureUnif.SetValue(1);
			
			device->BindBuffer(IGLDevice::ArrayBuffer, 0);
			
			static GLProgramAttribute positionAttribute("positionAttribute");
			static GLProgramAttribute ambientOcclusionCoordAttribute("ambientOcclusionCoordAttribute");
			static GLProgramAttribute colorAttribute("colorAttribute");
			static GLProgramAttribute normalAttribute("normalAttribute");
			
			positionAttribute(basicProgram);
			ambientOcclusionCoordAttribute(basicProgram);
			colorAttribute(basicProgram);
			normalAttribute(basicProgram);
			
			device->EnableVertexAttribArray(positionAttribute(), true);
			if(ambientOcclusionCoordAttribute() != -1)
				device->EnableVertexAttribArray(ambientOcclusionCoordAttribute(), true);
			device->EnableVertexAttribArray(colorAttribute(), true);
			if(normalAttribute() != -1)
				device->EnableVertexAttribArray(normalAttribute(), true);
			
			static GLProgramUniform projectionViewMatrix("projectionViewMatrix");
			projectionViewMatrix(basicProgram);
			projectionViewMatrix.SetValue(renderer->GetProjectionViewMatrix());
			
			static GLProgramUniform viewMatrix("viewMatrix");
			viewMatrix(basicProgram);
			viewMatrix.SetValue(renderer->GetViewMatrix());
			
			RealizeChunks(eye);
			
			// draw from nearest to farthest
			int cx = (int)floorf(eye.x) / GLMapChunk::Size;
			int cy = (int)floorf(eye.y) / GLMapChunk::Size;
			int cz = (int)floorf(eye.z) / GLMapChunk::Size;
			DrawColumnSunlight(cx, cy, cz, eye);
			for(int dist = 1; dist <= 128 / GLMapChunk::Size; dist++) {
				for(int x = cx - dist; x <= cx + dist; x++){
					DrawColumnSunlight(x, cy + dist, cz, eye);
					DrawColumnSunlight(x, cy - dist, cz, eye);
				}
				for(int y = cy - dist + 1; y <= cy + dist - 1; y++){
					DrawColumnSunlight(cx + dist, y, cz, eye);
					DrawColumnSunlight(cx - dist, y, cz, eye);
				}
			}
			
				
			device->EnableVertexAttribArray(positionAttribute(), false);
			if(ambientOcclusionCoordAttribute() != -1)
				device->EnableVertexAttribArray(ambientOcclusionCoordAttribute(), false);
			device->EnableVertexAttribArray(colorAttribute(), false);
			if(normalAttribute() != -1)
				device->EnableVertexAttribArray(normalAttribute(), false);
			
			device->ActiveTexture(1);
			device->BindTexture(IGLDevice::Texture2D, 0);
			device->ActiveTexture(0);
			device->BindTexture(IGLDevice::Texture2D, 0);
		}
		
		
		
		void GLMapRenderer::RenderDynamicLightPass(std::vector<GLDynamicLight> lights) {
			SPADES_MARK_FUNCTION();
			
			GLProfiler profiler(device, "Map");
			
			if(lights.empty())
				return;
			
			
			Vector3 eye = renderer->GetSceneDef().viewOrigin;
			
			device->ActiveTexture(0);
			detailImage->Bind(IGLDevice::Texture2D);
			
			device->Enable(IGLDevice::CullFace, true);
			device->Enable(IGLDevice::DepthTest, true);
			
			dlightProgram->Use();
			
			static GLProgramUniform fogDistance("fogDistance");
			fogDistance(dlightProgram);
			fogDistance.SetValue(renderer->GetFogDistance());
			
			static GLProgramUniform detailTextureUnif("detailTexture");
			detailTextureUnif(dlightProgram);
			detailTextureUnif.SetValue(0);
			
			device->BindBuffer(IGLDevice::ArrayBuffer, 0);
			
			static GLProgramAttribute positionAttribute("positionAttribute");
			static GLProgramAttribute colorAttribute("colorAttribute");
			static GLProgramAttribute normalAttribute("normalAttribute");
			
			positionAttribute(dlightProgram);
			colorAttribute(dlightProgram);
			normalAttribute(dlightProgram);
			
			device->EnableVertexAttribArray(positionAttribute(), true);
			device->EnableVertexAttribArray(colorAttribute(), true);
			device->EnableVertexAttribArray(normalAttribute(), true);
			
			static GLProgramUniform projectionViewMatrix("projectionViewMatrix");
			projectionViewMatrix(dlightProgram);
			projectionViewMatrix.SetValue(renderer->GetProjectionViewMatrix());
			
			static GLProgramUniform viewMatrix("viewMatrix");
			viewMatrix(dlightProgram);
			viewMatrix.SetValue(renderer->GetViewMatrix());
			
			RealizeChunks(eye);
			
			// draw from nearest to farthest
			int cx = (int)floorf(eye.x) / GLMapChunk::Size;
			int cy = (int)floorf(eye.y) / GLMapChunk::Size;
			int cz = (int)floorf(eye.z) / GLMapChunk::Size;
			DrawColumnDLight(cx, cy, cz, eye, lights);
			// TODO: optimize call
			//       ex. don't call a chunk'r render method if
			//           no dlight lights it
			for(int dist = 1; dist <= 128 / GLMapChunk::Size; dist++) {
				for(int x = cx - dist; x <= cx + dist; x++){
					DrawColumnDLight(x, cy + dist, cz, eye, lights);
					DrawColumnDLight(x, cy - dist, cz, eye, lights);
				}
				for(int y = cy - dist + 1; y <= cy + dist - 1; y++){
					DrawColumnDLight(cx + dist, y, cz, eye, lights);
					DrawColumnDLight(cx - dist, y, cz, eye, lights);
				}
			}
			
			
			device->EnableVertexAttribArray(positionAttribute(), false);
			device->EnableVertexAttribArray(colorAttribute(), false);
			device->EnableVertexAttribArray(normalAttribute(), false);
			
			device->ActiveTexture(0);
			device->BindTexture(IGLDevice::Texture2D, 0);
		}
		
		void GLMapRenderer::DrawColumnSunlight(int cx, int cy, int cz, spades::Vector3 eye){
			cx &= numChunkWidth -1;
			cy &= numChunkHeight - 1;
			for(int z = std::max(cz, 0); z < numChunkDepth; z++)
				GetChunk(cx, cy, z)->RenderSunlightPass();
			for(int z = std::min(cz - 1, 63); z >= 0; z--)
				GetChunk(cx, cy, z)->RenderSunlightPass();
		}
		
		void GLMapRenderer::DrawColumnDLight(int cx, int cy, int cz, spades::Vector3 eye, const std::vector<GLDynamicLight>& lights){
			cx &= numChunkWidth -1;
			cy &= numChunkHeight - 1;
			for(int z = std::max(cz, 0); z < numChunkDepth; z++)
				GetChunk(cx, cy, z)->RenderDLightPass(lights);
			for(int z = std::min(cz - 1, 63); z >= 0; z--)
				GetChunk(cx, cy, z)->RenderDLightPass(lights);
		}
	}
}
