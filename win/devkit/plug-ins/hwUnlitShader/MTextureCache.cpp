//-
// ==========================================================================
// Copyright (C) 1995 - 2006 Autodesk, Inc. and/or its licensors.  All 
// rights reserved.
//
// The coded instructions, statements, computer programs, and/or related 
// material (collectively the "Data") in these files contain unpublished 
// information proprietary to Autodesk, Inc. ("Autodesk") and/or its 
// licensors, which is protected by U.S. and Canadian federal copyright 
// law and by international treaties.
//
// The Data is provided for use exclusively by You. You have the right 
// to use, modify, and incorporate this Data into other products for 
// purposes authorized by the Autodesk software license agreement, 
// without fee.
//
// The copyright notices in the Software and this entire statement, 
// including the above license grant, this restriction and the 
// following disclaimer, must be included in all copies of the 
// Software, in whole or in part, and all derivative works of 
// the Software, unless such copies or derivative works are solely 
// in the form of machine-executable object code generated by a 
// source language processor.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND. 
// AUTODESK DOES NOT MAKE AND HEREBY DISCLAIMS ANY EXPRESS OR IMPLIED 
// WARRANTIES INCLUDING, BUT NOT LIMITED TO, THE WARRANTIES OF 
// NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR 
// PURPOSE, OR ARISING FROM A COURSE OF DEALING, USAGE, OR 
// TRADE PRACTICE. IN NO EVENT WILL AUTODESK AND/OR ITS LICENSORS 
// BE LIABLE FOR ANY LOST REVENUES, DATA, OR PROFITS, OR SPECIAL, 
// DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES, EVEN IF AUTODESK 
// AND/OR ITS LICENSORS HAS BEEN ADVISED OF THE POSSIBILITY 
// OR PROBABILITY OF SUCH DAMAGES.
//
// ==========================================================================
//+

// MTextureCache.cpp

///////////////////////////////////////////////////////////////////
// DESCRIPTION: Texture cache, used to temporarily store textures.
//				Eventually, we'll likely expose the actual Maya
//				texture cache to plug-ins to improve memory utilization
//				and performance.
//
// PS: Thanks to sbrew@lucasarts.com for contributing several fixes 
// to this class! ;-)
//
///////////////////////////////////////////////////////////////////

#include <maya/MPlug.h>
#include "MTextureCache.h"
#include "NodeMonitor.h"

// Initialize the singleton instance, and the refcount is originally 0.
MTextureCache* MTextureCache::m_instance = NULL;
/*static*/ int MTextureCache::refcount = 0;

MTextureCacheElement::~MTextureCacheElement()
{ 
	if (m_texture) 
	{
		delete m_texture;
		m_texture = NULL;
	}
}


MTextureCache::~MTextureCache()
{
	if (m_textureTable.empty())
		return;

	// Delete all texture cache elements.
	//
	string_to_cacheElement_map::iterator p = m_textureTable.begin();
	for ( ; p != m_textureTable.end(); ++p)
	{
		delete p->second;
	}
}

// Return a reference to the texture. Need to dereference by calling "release".
MTexture* MTextureCache::texture(MObject textureObj, 
			 MTexture::Type type /* = MTexture::RGBA */, 
			 bool mipmapped /* = true */,
			 GLenum target /* = GL_TEXTURE_2D */)
{
	// Get the name of the texture object.
	MString textureName = getNameFromObj(textureObj);
	
	// If this isn't a file texture node, or if it has no valid name, 
	// return NULL.
	if (!textureObj.hasFn(MFn::kFileTexture) ||
		textureName == "")
		return NULL;

	// Check if we already have a texCacheElement assigned to the given texture name.
	MTextureCacheElement *texCacheElement = 
		m_textureTable[textureName.asChar()];
	bool newTexture = !texCacheElement;
	bool textureDirty = texCacheElement && texCacheElement->fMonitor.dirty();

	if (textureDirty)
	{
		texCacheElement->fMonitor.stopWatching();
		delete texCacheElement->m_texture;
		texCacheElement->m_texture = NULL;
	}

	if (newTexture)
	{
		texCacheElement = new MTextureCacheElement;

		texCacheElement->fMonitor.setManager(this);
		
		// Add it to the map.
		m_textureTable[textureName.asChar()] = texCacheElement;
	}

	if (textureDirty || newTexture)
	{
		// Get the filename of the file texture node.
		MString textureFilename;
		MFnDependencyNode textureNode(textureObj);
		MPlug filenamePlug( textureObj, 
			textureNode.attribute(MString("fileTextureName")) );
		filenamePlug.getValue(textureFilename);
		
		// Create the MTexture
		texCacheElement->m_texture = new MTexture;

		// Monitor the given texture node for "dirty" or "rename" messages.
		texCacheElement->fMonitor.watch(textureObj);

		// Attempt to load the texture from disk and bind it in the OpenGL driver. 
		if (texCacheElement->m_texture->load(textureFilename, 
								type, mipmapped, target) == false)
		{
			// An error occured. Most likely, it was impossible to 
			// open the given filename.
			// Clean up and return NULL.
			delete texCacheElement;
			texCacheElement = NULL;
			m_textureTable.erase(textureName.asChar());
			return NULL;
		}
	}

	// Update the last updated timestamp.
	texCacheElement->lastAccessedTimestamp = m_currentTimestamp;

	return texCacheElement->texture();
}

// Returns true if the texture was found and bound; 
// returns false otherwise.
bool MTextureCache::bind(MObject textureObj, 
						 MTexture::Type type /* = MTexture::RGBA */, 
						 bool mipmapped /* = true */,
 						 GLenum target /* = GL_TEXTURE_2D */)
{
	// Get a reference to the texture, allocating it if necessary.
	MTexture* pTex = texture(textureObj, type, mipmapped, target);

	if (pTex)
	{
		// bind the texture.
		pTex->bind();
	
		return true;
	}
	
	return false;
}

void MTextureCache::onNodeRenamed(MObject& node, MString oldName, MString newName)
{
	// Remove the texture from the cache.
	MTextureCacheElement *texCacheElement = m_textureTable[oldName.asChar()];
	delete texCacheElement->m_texture;
	texCacheElement->m_texture = NULL;
}

void MTextureCache::incrementTimestamp(unsigned int increment /* =  1 */)
{
	m_currentTimestamp += increment;
	
	// Optionally, go through all textures and get rid of each of them that
	// is too old.
}
