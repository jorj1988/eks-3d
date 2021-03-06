#include "GL/XGLRenderer.h"
#include "Utilities/XFlags.h"
#include <iostream>
#include "QDebug"
#ifdef X_ENABLE_GL_RENDERER

#ifndef Q_OS_OSX
# define USE_GLEW
#endif

#ifdef Q_OS_OSX
# define STANDARD_OPENGL
# include <OpenGL/gl3.h>
#endif

#ifdef USE_GLEW
# include "GL/glew.h"
# define STANDARD_OPENGL
#endif

#ifdef X_GLES
# ifndef X_GLES
#  error Need X_GLES defined
# endif
# include "QGLFunctions"
#endif

#include "Containers/XStringSimple.h"
#include "Memory/XAllocatorBase.h"
#include "XFramebuffer.h"
#include "XGeometry.h"
#include "XShader.h"
#include "XTexture.h"
#include "XRasteriserState.h"
#include "XBlendState.h"
#include "XDepthStencilState.h"
#include "Math/XColour.h"
#include "XShader.h"
#include "Utilities/XParseException.h"
#include "Memory/XTemporaryAllocator.h"
#include "Containers/XStringBuffer.h"

#define GL_REND(x) static_cast<GLRendererImpl*>(x)

const char *glErrorString( int err )
  {
  if( err == GL_INVALID_ENUM )
    {
    return "GL Error: Invalid Enum";
    }
  else if( err == GL_INVALID_VALUE )
    {
    return "GL Error: Invalid Value";
    }
  else if( err == GL_INVALID_OPERATION )
    {
    return "GL Error: Invalid Operation";
    }
  else if( err == GL_OUT_OF_MEMORY )
    {
    return "GL Error: Out Of Memory";
    }
  else if ( err )
    {
    return "GL Error: Unknown";
    }
  return "GL Error: No Error";
  }

#ifdef X_DEBUG
# define GLE ; { int error = glGetError(); xAssertMessage(error == GL_NO_ERROR, "GL Error", error, glErrorString(error)); }
# define GLE_QUIET ; glGetError()
#else
# define GLE
# define GLE_QUIET
#endif

namespace Eks
{
class XGL21ShaderData;
class XGLVertexLayout;

template <typename X, typename T> void destroy(Renderer *, X *x)
  {
  x->template destroy<T>();
  }

//----------------------------------------------------------------------------------------------------------------------
// RENDERER
//----------------------------------------------------------------------------------------------------------------------

class GLRendererImpl : public Renderer
  {
public:
  GLRendererImpl(const detail::RendererFunctions& fns, int majorVersion, AllocatorBase *alloc);

  static void setTransform(Renderer *r, const Transform &trans)
    {
    GL_REND(r)->_modelData.model = trans.matrix();
    GL_REND(r)->_modelDataDirty = true;
    }

  static void setClearColour(Renderer *, const Colour &col)
    {
    glClearColor(col.x(), col.y(), col.z(), col.w()) GLE;
    }

  static void setViewTransform(Renderer *r, const Eks::Transform &trans)
    {
    GL_REND(r)->_viewData.view = trans.matrix();
    GL_REND(r)->_viewDataDirty = true;
    }

  static void setProjectionTransform(Renderer *r, const Eks::ComplexTransform &trans)
    {
    GL_REND(r)->_viewData.proj = trans.matrix();
    GL_REND(r)->_viewDataDirty = true;
    }

  template <xuint32 PRIMITIVE> static void drawIndexedPrimitive21(Renderer *r, const IndexGeometry *indices, const Geometry *vert);
  template <xuint32 PRIMITIVE> static void drawPrimitive21(Renderer *r, const Geometry *vert);
  template <xuint32 PRIMITIVE> static void drawIndexedPrimitive33(Renderer *r, const IndexGeometry *indices, const Geometry *vert);
  template <xuint32 PRIMITIVE> static void drawPrimitive33(Renderer *r, const Geometry *vert);

  static void drawPatch33(Renderer *r, const Geometry *vert, xuint8 vertCount);

  static void debugRenderLocator(Renderer *r, RendererDebugLocatorMode);

  static Shader *stockShader(Renderer *r, RendererShaderType t, const ShaderVertexLayout **);
  static void setStockShader(Renderer *, RendererShaderType, Shader *, const ShaderVertexLayout *);

  enum
    {
    ConstantBufferIndexOffset = 2
    };

  Eks::AllocatorBase *_allocator;

  Eks::ShaderConstantData _model;
  Eks::ShaderConstantData _view;

  struct ModelMatrices
    {
    Eks::Matrix4x4 model;
    Eks::Matrix4x4 modelView;
    Eks::Matrix4x4 modelViewProj;
    };
  struct ViewMatrices
    {
    Eks::Matrix4x4 view;
    Eks::Matrix4x4 proj;
    };
  ModelMatrices _modelData;
  ViewMatrices _viewData;
  bool _modelDataDirty;
  bool _viewDataDirty;

  void updateViewData();
  void (*setConstantBuffersInternal)(
        Renderer *r,
        Shader *shader,
        xsize index,
        xsize count,
        const ShaderConstantData * const* data);

  Shader *stockShaders[ShaderTypeCount];
  const ShaderVertexLayout *stockLayouts[ShaderTypeCount];

  Shader *_currentShader;
  ShaderVertexLayout *_vertexLayout;
  XGLFramebuffer *_currentFramebuffer;
  const char *_shaderHeader;
  };

//----------------------------------------------------------------------------------------------------------------------
// TEXTURE
//----------------------------------------------------------------------------------------------------------------------
class XGLShaderResource
  {
public:
  void bindResource(xuint32 active) const
    {
    glActiveTexture(GL_TEXTURE0 + active);
    glBindTexture(_type, _id);
    }

  xuint32 _type;
  xuint32 _id;
  };

//----------------------------------------------------------------------------------------------------------------------
// TEXTURE
//----------------------------------------------------------------------------------------------------------------------
class XGLTexture2D : public XGLShaderResource
  {
public:
  bool init(GLRendererImpl *, xuint32 format, xsize width, xsize height, const void *data);

  ~XGLTexture2D();

  static bool create(
      Renderer *r,
      Texture2D *tex,
      xsize w,
      xsize h,
      xuint32 format,
      const void *data)
    {
    XGLTexture2D* t = tex->create<XGLTexture2D>();
    return t->init(GL_REND(r), format, w, h, data);
    }

  static void getInfo(const Renderer *r, const Texture2D *tex, Eks::VectorUI2D& v);


  void bind() const
    {
    glBindTexture(GL_TEXTURE_2D, _id);
    }

  void unbind() const
    {
    glBindTexture(GL_TEXTURE_2D, 0);
    }

private:
  void clear();
  int getInternalFormat( int format );

  VectorUI2D size;

  friend class XGLFramebuffer;
  friend class XGLShaderVariable;
  };

//----------------------------------------------------------------------------------------------------------------------
// FRAMEBUFFER
//----------------------------------------------------------------------------------------------------------------------
class XGLFramebuffer
  {
public:
  bool initDefaultBuffer(GLRendererImpl *);

  const Texture2D *colour() const;
  const Texture2D *depth() const;

  static void clear(
      Renderer *X_USED_FOR_ASSERTS(r),
      FrameBuffer *X_USED_FOR_ASSERTS(buffer),
      xuint32 mode)
    {
#if X_ASSERTS_ENABLED
    GLRendererImpl *rend = GL_REND(r);
    XGLFramebuffer *fb = buffer->data<XGLFramebuffer>();
    xAssert(rend->_currentFramebuffer == fb);
#endif

    xuint32 mask = ((mode&FrameBuffer::ClearColour) != 0 ? GL_COLOR_BUFFER_BIT : 0) |
                   ((mode&FrameBuffer::ClearDepth) != 0 ? GL_DEPTH_BUFFER_BIT : 0);

    glClear(mask);
    }

  static bool resize(Renderer *, ScreenFrameBuffer *, xuint32 w, xuint32 h, xuint32)
    {
    glViewport(0,0,w,h);
    return true;
    }

  static void present(Renderer *, ScreenFrameBuffer *, bool *)
    {
    // swap buffers?
    }

  static Texture2D *getTexture(Renderer *, FrameBuffer *buffer, xuint32 mode)
    {
    xAssert(mode < FrameBuffer::TextureIdCount);
    XGLFramebuffer *fb = buffer->data<XGLFramebuffer>();
    return fb->_textures + mode;
    }

protected:
  Texture2D _textures[FrameBuffer::TextureIdCount];
  unsigned int _buffer;
  GLRendererImpl *_impl;
  };


class XGL21Framebuffer : public XGLFramebuffer
  {
public:
  bool init(Renderer *, TextureFormat colourFormat, TextureFormat depthFormat, xuint32 width, xuint32 height);
  ~XGL21Framebuffer( );

  static bool create(
      Renderer *r,
      FrameBuffer *b,
      xuint32 w,
      xuint32 h,
      xuint32 colourFormat,
      xuint32 depthFormat)
    {
    XGL21Framebuffer* fb = b->create<XGL21Framebuffer>();
    return fb->init(GL_REND(r), (TextureFormat)colourFormat, (TextureFormat)depthFormat, w, h );
    }

  static bool createViewport(
      Renderer *r,
      ScreenFrameBuffer *buffer)
    {
    XGL21Framebuffer* fb = buffer->create<XGL21Framebuffer>();
    return fb->initDefaultBuffer(GL_REND(r));
    }

  static void beginRender(Renderer *ren, FrameBuffer *fb)
    {
    GLRendererImpl *r = GL_REND(ren);
    xAssert(!r->_currentFramebuffer);

    XGL21Framebuffer *fbImpl = fb->data<XGL21Framebuffer>();
    xAssert(fb);
    r->_currentFramebuffer = fbImpl;
    fbImpl->bind(r);

    clear(ren, fb, FrameBuffer::ClearColour|FrameBuffer::ClearDepth);
    }

  static void endRender(Renderer *ren, FrameBuffer *fb)
    {
    GLRendererImpl *r = GL_REND(ren);

    XGL21Framebuffer *iFb = fb->data<XGL21Framebuffer>();
    xAssert(r->_currentFramebuffer == iFb);

    if(r->_currentFramebuffer)
      {
      iFb->unbind(r);
      r->_currentFramebuffer = 0;
      }
    }

  void bind(GLRendererImpl *r);
  void unbind(GLRendererImpl *r);

  bool isValid(GLRendererImpl *impl) const;
  };


class XGL33Framebuffer : public XGLFramebuffer
  {
public:
  bool init(Renderer *, TextureFormat colourFormat, TextureFormat depthFormat, xuint32 width, xuint32 height);
  ~XGL33Framebuffer( );

  static void beginRender(Renderer *ren, FrameBuffer *fb)
    {
    GLRendererImpl *r = GL_REND(ren);
    xAssert(!r->_currentFramebuffer);

    XGL33Framebuffer *fbImpl = fb->data<XGL33Framebuffer>();
    xAssert(fb);
    r->_currentFramebuffer = fbImpl;
    fbImpl->bind(r);

    clear(ren, fb, FrameBuffer::ClearColour|FrameBuffer::ClearDepth);
    }

  static void endRender(Renderer *ren, FrameBuffer *fb)
    {
    GLRendererImpl *r = GL_REND(ren);

    XGL33Framebuffer *iFb = fb->data<XGL33Framebuffer>();
    xAssert(r->_currentFramebuffer == iFb);

    if(r->_currentFramebuffer)
      {
      iFb->unbind(r);
      r->_currentFramebuffer = 0;
      }
    }

  static bool create(
      Renderer *r,
      FrameBuffer *b,
      xuint32 w,
      xuint32 h,
      xuint32 colourFormat,
      xuint32 depthFormat)
    {
    XGL33Framebuffer* fb = b->create<XGL33Framebuffer>();
    return fb->init(GL_REND(r), (TextureFormat)colourFormat, (TextureFormat)depthFormat, w, h );
    }

  static bool createViewport(
      Renderer *r,
      ScreenFrameBuffer *buffer)
    {
    XGL33Framebuffer* fb = buffer->create<XGL33Framebuffer>();
    return fb->initDefaultBuffer(GL_REND(r));
    }

  void bind(GLRendererImpl *r);
  void unbind(GLRendererImpl *r);

  bool isValid(GLRendererImpl *impl) const;
  };

//----------------------------------------------------------------------------------------------------------------------
// BUFFER
//----------------------------------------------------------------------------------------------------------------------
class XGLBuffer
  {
public:
  bool init(GLRendererImpl *, const void *data, xuint32 type, xuint32 renderType, xsize size);
  ~XGLBuffer();

  unsigned int _buffer;
  };

//----------------------------------------------------------------------------------------------------------------------
// INDEX GEOMETRY CACHE
//----------------------------------------------------------------------------------------------------------------------
class XGLIndexGeometryCache : public XGLBuffer
  {
public:
  bool init(GLRendererImpl *, const void *data, IndexGeometry::Type type, xsize elementCount);

  static bool create(
      Renderer *ren,
      IndexGeometry *g,
      int elementType,
      const void *data,
      xsize elementCount)
    {
    XGLIndexGeometryCache *cache = g->create<XGLIndexGeometryCache>();
    cache->init(GL_REND(ren), data, (IndexGeometry::Type)elementType, elementCount);
    return true;
    }

  GLuint _indexCount;
  unsigned int _indexType;
  };

//----------------------------------------------------------------------------------------------------------------------
// GEOMETRY CACHE
//----------------------------------------------------------------------------------------------------------------------
class XGLGeometryCache : public XGLBuffer
  {
public:
  bool init( GLRendererImpl *, const void *data, xsize elementSize, xsize elementCount );

  static bool create(
      Renderer *ren,
      Geometry *g,
      const void *data,
      xsize elementSize,
      xsize elementCount)
    {
    XGLGeometryCache *cache = g->create<XGLGeometryCache>();
    cache->init(GL_REND(ren), data, elementSize, elementCount);
    return true;
    }

  GLuint _elementCount;
  GLuint _elementSize;

  mutable GLuint _vao;
  mutable const XGLVertexLayout *_linkedLayout;
  mutable const XGLIndexGeometryCache *_linkedIndices;
  };

//----------------------------------------------------------------------------------------------------------------------
// SHADER COMPONENT
//----------------------------------------------------------------------------------------------------------------------

class XGLShaderComponent
  {
public:
  bool init(
      GLRendererImpl *,
      xuint32 type,
      const char *data,
      xsize size,
      ParseErrorInterface *ifc);

  static bool create(
      Renderer *r,
      ShaderComponent *f,
      xuint32 type,
      const char *s,
      xsize l,
      ParseErrorInterface *ifc,
      const void *d)
    {
    XGLShaderComponent *glS = f->create<XGLShaderComponent>();
    glS->_layout = 0;


    bool res = glS->init(GL_REND(r), type, s, l, ifc);
    if (res && type == ShaderComponent::Vertex)
      {
      auto data = (const ShaderVertexComponent::ExtraCreateData *)d;
      return initVertex(r, f, data->vertexDescriptions, data->vertexItemCount, data->layout);
      }

    return res;
    }

  static bool initVertex(Renderer *r,
      ShaderComponent *v,
      const ShaderVertexLayoutDescription *vertexDescriptions,
      xsize vertexItemCount,
      ShaderVertexLayout *layout);

  xuint32 _component;
  XGLVertexLayout* _layout;
  };

//----------------------------------------------------------------------------------------------------------------------
// SHADER
//----------------------------------------------------------------------------------------------------------------------
class XGLShader
  {
public:
  bool init(GLRendererImpl *impl,
    ShaderComponent **v,
    xsize shaderCount,
    const char **outputs,
    xsize outputCount,
    ParseErrorInterface *errors);

  ~XGLShader();

  static void destroy(Renderer *r, Shader *x)
    {
    if(x == GL_REND(r)->_currentShader)
      {
      glUseProgram(0);
      GL_REND(r)->_currentShader = 0;
      }

    Eks::destroy<Shader, XGLShader>(r, x);
    }

  static bool create(
      Renderer *r,
      Shader *s,
      ShaderComponent **cmp,
      xsize shaderCount,
      const char **outputs,
      xsize outputCount,
      ParseErrorInterface *errors)
    {
    XGLShader *glS = s->create<XGLShader>();
    return glS->init(GL_REND(r), cmp, shaderCount, outputs, outputCount, errors);
    }

  static void bind(Renderer *ren, const Shader *shader, const ShaderVertexLayout *layout);

  void bindDumb(Renderer *ren);

  static void setConstantBuffers21(
      Renderer *r,
      Shader *shader,
      xsize index,
      xsize count,
      const ShaderConstantData * const* data)
    {
    setConstantBuffersInternal21(r, shader, index + GLRendererImpl::ConstantBufferIndexOffset, count, data);
    }

  static void setConstantBuffersInternal21(
      Renderer *r,
      Shader *shader,
      xsize index,
      xsize count,
      const ShaderConstantData * const* data);

  static void setConstantBuffers33(
      Renderer *r,
      Shader *shader,
      xsize index,
      xsize count,
      const ShaderConstantData * const* data)
    {
    setConstantBuffersInternal33(r, shader, index + GLRendererImpl::ConstantBufferIndexOffset, count, data);
    }

  static void setConstantBuffersInternal33(
      Renderer *r,
      Shader *shader,
      xsize index,
      xsize count,
      const ShaderConstantData * const* data);

  static void setResources21(
      Renderer *r,
      Shader *shader,
      xsize index,
      xsize count,
      const Resource * const* data);

  static void setResources33(
      Renderer *r,
      Shader *shader,
      xsize index,
      xsize count,
      const Resource * const* data);

  GLuint shader;
  xuint8 maxSetupResources;

  struct Buffer
    {
    Buffer() : data(0), revision(0) { }
    XGL21ShaderData *data;
    xuint8 revision;
    };

  Eks::Vector<Buffer> _buffers;
  friend class XGLRenderer;
  friend class XGLShaderVariable;
  };

//----------------------------------------------------------------------------------------------------------------------
// VERTEX LAYOUT
//----------------------------------------------------------------------------------------------------------------------
class XGLVertexLayout
  {
public:
  bool init1(GLRendererImpl *r, const ShaderVertexLayoutDescription *descs, xsize count)
    {
    _renderer = r;
    xAssert(count < std::numeric_limits<xuint8>::max());
    _attrCount = (xuint8)count;
    xAssert(count < ShaderVertexLayoutDescription::SemanticCount)

    vertexSize = 0;
    for(xsize i = 0; i < count; ++i)
      {
      const ShaderVertexLayoutDescription &desc = descs[i];
      Attribute &attr = _attrs[i];

      xAssert(desc.offset < std::numeric_limits<xuint8>::max() || desc.offset == ShaderVertexLayoutDescription::OffsetPackTight);
      attr.offset = (xuint8)desc.offset;
      attr.semantic = desc.semantic;
      if(desc.offset == ShaderVertexLayoutDescription::OffsetPackTight)
        {
        attr.offset = (xuint8)vertexSize;
        }

      xCompileTimeAssert(ShaderVertexLayoutDescription::FormatFloat1 == 0);
      xCompileTimeAssert(ShaderVertexLayoutDescription::FormatFloat2 == 1);
      xCompileTimeAssert(ShaderVertexLayoutDescription::FormatFloat3 == 2);
      xCompileTimeAssert(ShaderVertexLayoutDescription::FormatFloat4 == 3);
      attr.components = desc.format + 1;
      xAssert(attr.components <= 4);

      xAssert(vertexSize < std::numeric_limits<xuint8>::max());
      vertexSize = std::max(vertexSize, (xuint8)(attr.offset + attr.size()));
      }

    return true;
    }

  bool init2(GLRendererImpl *, XGLShader* shader)
    {
    const char *semanticNames[] =
    {
      "position",
      "colour",
      "textureCoordinate",
      "normal",
      "binormal"
    };
    xCompileTimeAssert(X_ARRAY_COUNT(semanticNames) == ShaderVertexLayoutDescription::SemanticCount);

    for(GLuint i = 0; i < (GLuint)_attrCount; ++i)
      {
      const Attribute &attr = _attrs[i];
      xsize idx = attr.semantic;

      glBindAttribLocation(shader->shader, i, semanticNames[idx]) GLE;
      }

    return true;
    }

  xuint8 vertexSize;
  struct Attribute
    {
    xuint8 offset;
    xuint8 components;
    xuint8 semantic;
    // type is currently always float.

    inline xsize size() const
      {
      return components * sizeof(float);
      }
    };

  Attribute _attrs[ShaderVertexLayoutDescription::SemanticCount];
  xuint8 _attrCount;
  Eks::GLRendererImpl* _renderer;

  void bindVertexData(const XGLGeometryCache *X_USED_FOR_ASSERTS(cache)) const
    {
    xAssert(cache->_elementSize == vertexSize, cache->_elementSize, (int)vertexSize);
    for(GLuint i = 0, s = (GLuint)_attrCount; i < s; ++i)
      {
      const Attribute &attr = _attrs[i];

      xsize offset = (xsize)attr.offset;

      glEnableVertexAttribArray(i) GLE;
      glVertexAttribPointer(
            i,
            attr.components,
            GL_FLOAT,
            GL_FALSE,
            vertexSize,
            (GLvoid*)offset) GLE;
      }
    }

  void unbindVertexData() const
    {
    for(GLuint i = 0, s = (GLuint)_attrCount; i < s; ++i)
      {
      glDisableVertexAttribArray(i) GLE;
      }
    }

#ifdef STANDARD_OPENGL
  void bindVAO(const XGLGeometryCache *cache, const XGLIndexGeometryCache *indexedCache)
    {
    // Currently a geometry cache must be used with only one layout.
    if(!cache->_linkedLayout)
      {
      cache->_linkedLayout = this;
      cache->_linkedIndices = indexedCache;

      xAssert(!cache->_vao);
      glGenVertexArrays(1, &cache->_vao) GLE;
      glBindVertexArray(cache->_vao) GLE;

      glBindBuffer(GL_ARRAY_BUFFER, cache->_buffer) GLE;
      bindVertexData(cache);
      if(indexedCache)
        {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexedCache->_buffer) GLE;
        }
      }
    else
      {
      xAssert(cache->_linkedIndices == indexedCache);
      xAssert(cache->_vao);
      glBindVertexArray(cache->_vao) GLE;
      }
    }

  void unbindVAO()
    {
    glBindVertexArray(0) GLE;
    }
#endif
  };

//----------------------------------------------------------------------------------------------------------------------
// SHADER DATA
//----------------------------------------------------------------------------------------------------------------------
class XGL21ShaderData
  {
public:
  bool init(GLRendererImpl *, ShaderConstantDataDescription *desc, xsize descCount, const void *data);

  static void update(Renderer *r, ShaderConstantData *, void *data);

  static bool create(
      Renderer *r,
      ShaderConstantData *d,
      ShaderConstantDataDescription *desc,
      xsize descCount,
      const void *data)
    {
    XGL21ShaderData *glD = d->create<XGL21ShaderData>();
    return glD->init(GL_REND(r), desc, descCount, data);
    }

  void bind(xuint32 program, xuint32 index) const;

  typedef void (*BindFunction)(xuint32 location, const xuint8* data);
  struct Binder
    {
    Eks::String name;
    BindFunction bind;
    xsize offset;
    };

  Vector<xuint8> _data;
  Vector<Binder> _binders;
  xuint8 _revision;

  friend class XGLRenderer;
  };

class XGL33ShaderData : public XGLBuffer
  {
public:
  bool init(GLRendererImpl *, ShaderConstantDataDescription *desc, xsize descCount, const void *data);

  static void update(Renderer *r, ShaderConstantData *, void *data);

  static bool create(
      Renderer *r,
      ShaderConstantData *d,
      ShaderConstantDataDescription *desc,
      xsize descCount,
      const void *data)
    {
    XGL33ShaderData *glD = d->create<XGL33ShaderData>();
    return glD->init(GL_REND(r), desc, descCount, data);
    }

  void bind(xuint32 program, xuint32 index) const;

  xsize _size;

  friend class XGLRenderer;
  };

//----------------------------------------------------------------------------------------------------------------------
// BLEND STATE
//----------------------------------------------------------------------------------------------------------------------
class XGLBlendState
  {
public:
  static void bind(Renderer *, const BlendState *state)
    {
    const XGLBlendState* s = state->data<XGLBlendState>();

    if (s->_enable)
      {
      glEnable(GL_BLEND);

      const int index = 0;

      glBlendEquationSeparatei(index, s->_modeRGB, s->_modeAlpha);
      glBlendFuncSeparatei(index, s->_srcRGB, s->_dstRGB, s->_srcAlpha, s->_dstAlpha);
      glBlendColor(s->_colour[0], s->_colour[1], s->_colour[2], s->_colour[3]);
      }
    else
      {
      glDisable(GL_BLEND);
      }	
    }

  static bool create(
      Renderer *,
      BlendState *state,
      bool enable,
      xuint32 modeRGB,
      xuint32 srcRGB,
      xuint32 dstRGB,
      xuint32 modeAlpha,
      xuint32 srcAlpha,
      xuint32 dstAlpha,
      const Eks::Colour &col)
    {
    XGLBlendState* s = state->create<XGLBlendState>();

    xuint32 modeMap[] = {
      GL_FUNC_ADD,
      GL_FUNC_SUBTRACT,
      GL_FUNC_REVERSE_SUBTRACT,
      GL_MIN,
      GL_MAX
    };
    xCompileTimeAssert(X_ARRAY_COUNT(modeMap) == Eks::BlendState::ModeCount);

    xuint32 parameterMap[] = {
      GL_ZERO,
      GL_ONE,
      GL_SRC_COLOR,
      GL_ONE_MINUS_SRC_COLOR,
      GL_DST_COLOR,
      GL_ONE_MINUS_DST_COLOR,
      GL_SRC_ALPHA,
      GL_ONE_MINUS_SRC_ALPHA,
      GL_DST_ALPHA,
      GL_ONE_MINUS_DST_ALPHA,
      GL_CONSTANT_COLOR,
      GL_ONE_MINUS_CONSTANT_COLOR,
      GL_CONSTANT_ALPHA,
      GL_ONE_MINUS_CONSTANT_ALPHA,
      GL_SRC_ALPHA_SATURATE,
      GL_SRC1_COLOR,
      GL_ONE_MINUS_SRC_COLOR,
      GL_SRC1_ALPHA,
      GL_ONE_MINUS_SRC_ALPHA,
    };
    xCompileTimeAssert(X_ARRAY_COUNT(parameterMap) == Eks::BlendState::ParameterCount);

    s->_enable = enable;
    s->_modeRGB = modeMap[modeRGB];
    s->_srcRGB = parameterMap[srcRGB];
    s->_dstRGB = parameterMap[dstRGB];
    s->_modeAlpha = modeMap[modeAlpha];
    s->_srcAlpha = parameterMap[srcAlpha];
    s->_dstAlpha = parameterMap[dstAlpha];
    s->_colour[0] = col.x();
    s->_colour[1] = col.y();
    s->_colour[2] = col.z();
    s->_colour[3] = col.w();

    return true;
    }

  bool _enable;
  xuint32 _modeRGB;
  xuint32 _srcRGB;
  xuint32 _dstRGB;
  xuint32 _modeAlpha;
  xuint32 _srcAlpha;
  xuint32 _dstAlpha;
  float _colour[4];
  };

//----------------------------------------------------------------------------------------------------------------------
// DEPTH STENCIL STATE
//----------------------------------------------------------------------------------------------------------------------
class XGLDepthStencilState
  {
public:
  bool init(GLRendererImpl *);

  static void bind(Renderer *, const DepthStencilState *state)
    {
    const XGLDepthStencilState* s = state->data<XGLDepthStencilState>();

    glColorMask(
      s->_enableColourRWrite,
      s->_enableColourGWrite,
      s->_enableColourBWrite,
      s->_enableColourAWrite) GLE;
    glDepthMask(s->_enableDepthWrite) GLE;
    glDepthMask(s->_enableStencilWrite) GLE;

    if (s->_testDepth)
      {
      glEnable(GL_DEPTH_TEST) GLE;
      }
    else
      {
      glDisable(GL_DEPTH_TEST) GLE;
      }

    if (s->_testStencil)
      {
      glEnable(GL_STENCIL_TEST) GLE;
      }
    else
      {
      glDisable(GL_STENCIL_TEST) GLE;
      }

    glDepthFunc(s->_depthFn);
    glStencilFunc(s->_stencilFn, s->_stencilRef, s->_stencilMask);

    glDepthRange(s->_depthNear, s->_depthFar);
    }

  static bool create(
      Renderer *,
      DepthStencilState *state,
      xuint32 writeMask,
      xuint32 tests,
      xuint32 depthFn,
      xuint32 stencilFn,
      xint32 stencilRef,
      xuint32 stencilMask,
      float depthNear,
      float depthFar
      )
    {
    XGLDepthStencilState* s = state->create<XGLDepthStencilState>();

    xuint32 functionMap[] = {
      GL_NEVER,
      GL_LESS,
      GL_LEQUAL,
      GL_GREATER,
      GL_GEQUAL,
      GL_EQUAL,
      GL_NOTEQUAL,
      GL_ALWAYS
    };
    xCompileTimeAssert(X_ARRAY_COUNT(functionMap) == Eks::DepthStencilState::FunctionCount);

    s->_enableColourRWrite = writeMask&Eks::DepthStencilState::ColourR;
    s->_enableColourGWrite = writeMask&Eks::DepthStencilState::ColourG;
    s->_enableColourBWrite = writeMask&Eks::DepthStencilState::ColourB;
    s->_enableColourAWrite = writeMask&Eks::DepthStencilState::ColourA;
    s->_enableDepthWrite = writeMask&Eks::DepthStencilState::Depth;
    s->_enableStencilWrite = writeMask&Eks::DepthStencilState::Stencil;

    s->_testDepth = tests&Eks::DepthStencilState::DepthTest;
    s->_testStencil = tests&Eks::DepthStencilState::StencilTest;

    s->_depthFn = functionMap[depthFn];
    s->_stencilFn = functionMap[stencilFn];

    s->_depthNear = depthNear;
    s->_depthFar = depthFar;

    s->_stencilRef = stencilRef;
    s->_stencilMask = stencilMask;

    return false;
    }

  bool _enableColourRWrite : 1;
  bool _enableColourGWrite : 1;
  bool _enableColourBWrite : 1;
  bool _enableColourAWrite : 1;
  bool _enableDepthWrite : 1;
  bool _enableStencilWrite : 1;

  bool _testDepth : 1;
  bool _testStencil : 1;

  xuint32 _depthFn;
  xuint32 _stencilFn;

  float _depthNear;
  float _depthFar;

  xint32 _stencilRef;
  xuint32 _stencilMask;
  };

//----------------------------------------------------------------------------------------------------------------------
// RASTERISER STATE
//----------------------------------------------------------------------------------------------------------------------
class XGLRasteriserState
  {
public:
  bool init(GLRendererImpl *, RasteriserState::CullMode mode)
    {
    _cull = mode;
    return true;
    }

  static void bind(Renderer *, const RasteriserState *state)
    {
    const XGLRasteriserState* s = state->data<XGLRasteriserState>();
    switch(s->_cull)
      {
    case RasteriserState::CullFront:
      glEnable(GL_CULL_FACE);
      glCullFace(GL_FRONT);
      break;
    case RasteriserState::CullBack:
      glEnable(GL_CULL_FACE);
      glCullFace(GL_BACK);
      break;
    case RasteriserState::CullNone:
    default:
      glDisable(GL_CULL_FACE);
      break;
      }
    }


  static bool create(
      Renderer *r,
      RasteriserState *s,
      xuint32 cull)
    {
    XGLRasteriserState *glS = s->create<XGLRasteriserState>();
    return glS->init(GL_REND(r), (RasteriserState::CullMode)cull);
    }

  RasteriserState::CullMode _cull;
  };

GLRendererImpl::GLRendererImpl(const detail::RendererFunctions &fns, int majorVersion, Eks::AllocatorBase *alloc)
  : _allocator(alloc),
    _modelDataDirty(true),
    _viewDataDirty(true),
    _currentShader(0),
    _vertexLayout(0),
    _currentFramebuffer(0)
  {
  _modelData.model = Eks::Matrix4x4::Identity();
  setFunctions(fns);

  if (majorVersion == 2)
    {
    _shaderHeader = "#version 120\n"
                    "#define X_GLSL_VERSION 120\n"
  #ifdef X_GLES
                    "#define X_GLES\n"
  #endif
      ;
    setConstantBuffersInternal = XGLShader::setConstantBuffersInternal21;
    }
  else if(majorVersion == 3)
    {
    _shaderHeader = "#version 330\n"
                    "#define X_GLSL_VERSION 330\n"
                    "#line 1\n";
    setConstantBuffersInternal = XGLShader::setConstantBuffersInternal33;
    }
  else if(majorVersion == 4)
    {
    _shaderHeader = "#version 410\n"
                    "#define X_GLSL_VERSION 410\n"
                    "#line 1\n";
    setConstantBuffersInternal = XGLShader::setConstantBuffersInternal33;
    }
  }

void GLRendererImpl::debugRenderLocator(Renderer *r, RendererDebugLocatorMode m)
  {
  if((m&RendererDebugLocatorMode::DebugLocatorClearShader) != 0)
    {
    GL_REND(r)->_currentShader = 0;
    glUseProgram(0);
    }

  float lineData[] =
  {
    -0.5, 0, 0,
    0.5, 0, 0,
    0, -0.5, 0,
    0, 0.5, 0,
    0, 0, -0.5,
    0, 0, 0.5
  };

  glEnableVertexAttribArray(0) GLE;
  glVertexAttribPointer(0, 2, GL_FLOAT, false, sizeof(float) * 3, lineData) GLE;
  glDrawArrays(GL_LINES, 0, 6) GLE;
  glDisableVertexAttribArray(0);

  float triData[] =
  {
    -0.5, 0, 0,
    0, 0, 0,
    0, -0.5, 0,
    0, 0, 0,
    -0.5, 0, 0,
    0, -0.5, 0,
  };

  glEnableVertexAttribArray(0) GLE;
  glVertexAttribPointer(0, 2, GL_FLOAT, false, sizeof(float) * 3, triData) GLE;
  glDrawArrays(GL_TRIANGLES, 0, 6) GLE;
  glDisableVertexAttribArray(0);
  }

void GLRendererImpl::updateViewData()
  {
  if(_viewDataDirty)
    {
    _view.update(&_viewData);
    _modelDataDirty = true;
    }

  if(_modelDataDirty)
    {
    _modelData.modelView = _viewData.view * _modelData.model;
    _modelData.modelViewProj = _viewData.proj * _modelData.modelView;
    _model.update(&_modelData);
    }

  ShaderConstantData *data[] =
  {
    &_model,
    &_view
  };
  xAssert(_currentShader);
  setConstantBuffersInternal(this, _currentShader, 0, 2, data);
  }

template <xuint32 PRIMITIVE> void GLRendererImpl::drawIndexedPrimitive21(
    Renderer *ren,
    const IndexGeometry *indices,
    const Geometry *vert)
  {
  GLRendererImpl* r = GL_REND(ren);
  xAssert(r->_currentShader);
  xAssert(r->_vertexLayout);
  xAssert(indices);
  xAssert(vert);

  const XGLIndexGeometryCache *idx = indices->data<XGLIndexGeometryCache>();
  const XGLGeometryCache *gC = vert->data<XGLGeometryCache>();


  r->updateViewData();

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, idx->_buffer) GLE;
  glBindBuffer(GL_ARRAY_BUFFER, gC->_buffer) GLE;

  XGLVertexLayout *l = r->_vertexLayout->data<XGLVertexLayout>();
  l->bindVertexData(gC);

  glDrawElements(PRIMITIVE, idx->_indexCount, idx->_indexType, (GLvoid*)((char*)NULL)) GLE;
  l->unbindVertexData();

  glBindBuffer(GL_ARRAY_BUFFER, 0) GLE;
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0) GLE;
  }

template <xuint32 PRIMITIVE> void GLRendererImpl::drawIndexedPrimitive33(
    Renderer *ren,
    const IndexGeometry *indices,
    const Geometry *vert)
  {
  GLRendererImpl* r = GL_REND(ren);
  xAssert(r->_currentShader);
  xAssert(r->_vertexLayout);
  xAssert(indices);
  xAssert(vert);

  const XGLIndexGeometryCache *idx = indices->data<XGLIndexGeometryCache>();
  const XGLGeometryCache *gC = vert->data<XGLGeometryCache>();

  r->updateViewData();

  XGLVertexLayout *l = r->_vertexLayout->data<XGLVertexLayout>();
  l->bindVAO(gC, idx);

  glDrawElements(PRIMITIVE, idx->_indexCount, idx->_indexType, (GLvoid*)((char*)NULL)) GLE;

  l->unbindVAO();
  }

template <xuint32 PRIMITIVE> void GLRendererImpl::drawPrimitive21(Renderer *ren, const Geometry *vert)
  {
  GLRendererImpl* r = GL_REND(ren);
  xAssert(r->_currentShader);
  xAssert(r->_vertexLayout);
  xAssert(vert);

  const XGLGeometryCache *gC = vert->data<XGLGeometryCache>();

  r->updateViewData();

  glBindBuffer( GL_ARRAY_BUFFER, gC->_buffer ) GLE;

  XGLVertexLayout *l = r->_vertexLayout->data<XGLVertexLayout>();
  l->bindVertexData(gC);

  glDrawArrays(PRIMITIVE, 0, gC->_elementCount) GLE;
  l->unbindVertexData();

  glBindBuffer( GL_ARRAY_BUFFER, 0 ) GLE;
  }

void GLRendererImpl::drawPatch33(Renderer *r, const Geometry *vert, xuint8 vertCount)
  {
  glPatchParameteri(GL_PATCH_VERTICES, vertCount);
  drawPrimitive33<GL_PATCHES>(r, vert);
  }

template <xuint32 PRIMITIVE> void GLRendererImpl::drawPrimitive33(Renderer *ren, const Geometry *vert)
  {
  GLRendererImpl* r = GL_REND(ren);
  xAssert(r->_currentShader);
  xAssert(r->_vertexLayout);
  xAssert(vert);

  const XGLGeometryCache *gC = vert->data<XGLGeometryCache>();

  r->updateViewData();

  XGLVertexLayout *l = r->_vertexLayout->data<XGLVertexLayout>();
  l->bindVAO(gC, nullptr);

  glDrawArrays(PRIMITIVE, 0, gC->_elementCount) GLE;
  l->unbindVAO();
  }

Shader *GLRendererImpl::stockShader(Renderer *r, RendererShaderType t, const ShaderVertexLayout **l)
  {
  xAssert(l);
  xAssert(GL_REND(r)->stockLayouts[t]);
  xAssert(GL_REND(r)->stockShaders[t]);

  *l = GL_REND(r)->stockLayouts[t];
  return GL_REND(r)->stockShaders[t];
  }

void GLRendererImpl::setStockShader(
    Renderer *r,
    RendererShaderType t,
    Shader *s,
    const ShaderVertexLayout *l)
  {
  GL_REND(r)->stockShaders[t] = s;
  GL_REND(r)->stockLayouts[t] = l;
  }

detail::RendererFunctions gl21fns =
{
  {
    XGL21Framebuffer::create,
    XGL21Framebuffer::createViewport,
    XGLGeometryCache::create,
    XGLIndexGeometryCache::create,
    XGLTexture2D::create,
    XGLShader::create,
    XGLShaderComponent::create,
    XGLRasteriserState::create,
    XGLDepthStencilState::create,
    XGLBlendState::create,
    XGL21ShaderData::create
  },
  {
    destroy<FrameBuffer, XGL21Framebuffer>,
    destroy<Geometry, XGLGeometryCache>,
    destroy<IndexGeometry, XGLIndexGeometryCache>,
    destroy<Texture2D, XGLTexture2D>,
    XGLShader::destroy,
    destroy<ShaderVertexLayout, XGLVertexLayout>,
    destroy<ShaderComponent, XGLShaderComponent>,
    destroy<RasteriserState, XGLRasteriserState>,
    destroy<DepthStencilState, XGLDepthStencilState>,
    destroy<BlendState, XGLBlendState>,
    destroy<ShaderConstantData, XGL21ShaderData>
  },
  {
    GLRendererImpl::setClearColour,
    XGL21ShaderData::update,
    GLRendererImpl::setViewTransform,
    GLRendererImpl::setProjectionTransform,
    XGLShader::setConstantBuffers21,
    XGLShader::setResources21,
    XGLShader::bind,
    XGLRasteriserState::bind,
    XGLDepthStencilState::bind,
    XGLBlendState::bind,
    GLRendererImpl::setTransform,
    GLRendererImpl::setStockShader
  },
  {
    XGLTexture2D::getInfo,
    GLRendererImpl::stockShader,
  },
  {
    GLRendererImpl::drawIndexedPrimitive21<GL_TRIANGLES>,
    GLRendererImpl::drawPrimitive21<GL_TRIANGLES>,
    GLRendererImpl::drawPatch33,
    GLRendererImpl::drawIndexedPrimitive21<GL_LINES>,
    GLRendererImpl::drawPrimitive21<GL_LINES>,
    GLRendererImpl::debugRenderLocator
  },
  {
    XGL21Framebuffer::clear,
    XGL21Framebuffer::resize,
    XGL21Framebuffer::beginRender,
    XGL21Framebuffer::endRender,
    XGL21Framebuffer::present,
    XGL21Framebuffer::getTexture
  }
};

#ifdef STANDARD_OPENGL
detail::RendererFunctions gl33fns =
{
  {
    XGL33Framebuffer::create,
    XGL33Framebuffer::createViewport,
    XGLGeometryCache::create,
    XGLIndexGeometryCache::create,
    XGLTexture2D::create,
    XGLShader::create,
    XGLShaderComponent::create,
    XGLRasteriserState::create,
    XGLDepthStencilState::create,
    XGLBlendState::create,
    XGL33ShaderData::create
  },
  {
    destroy<FrameBuffer, XGL33Framebuffer>,
    destroy<Geometry, XGLGeometryCache>,
    destroy<IndexGeometry, XGLIndexGeometryCache>,
    destroy<Texture2D, XGLTexture2D>,
    XGLShader::destroy,
    destroy<ShaderVertexLayout, XGLVertexLayout>,
    destroy<ShaderComponent, XGLShaderComponent>,
    destroy<RasteriserState, XGLRasteriserState>,
    destroy<DepthStencilState, XGLDepthStencilState>,
    destroy<BlendState, XGLBlendState>,
    destroy<ShaderConstantData, XGL33ShaderData>
  },
  {
    GLRendererImpl::setClearColour,
    XGL33ShaderData::update,
    GLRendererImpl::setViewTransform,
    GLRendererImpl::setProjectionTransform,
    XGLShader::setConstantBuffers33,
    XGLShader::setResources21,
    XGLShader::bind,
    XGLRasteriserState::bind,
    XGLDepthStencilState::bind,
    XGLBlendState::bind,
    GLRendererImpl::setTransform,
    GLRendererImpl::setStockShader
  },
  {
    XGLTexture2D::getInfo,
    GLRendererImpl::stockShader,
  },
  {
    GLRendererImpl::drawIndexedPrimitive33<GL_TRIANGLES>,
    GLRendererImpl::drawPrimitive33<GL_TRIANGLES>,
    GLRendererImpl::drawPatch33,
    GLRendererImpl::drawIndexedPrimitive33<GL_LINES>,
    GLRendererImpl::drawPrimitive33<GL_LINES>,
    GLRendererImpl::debugRenderLocator
  },
  {
    XGL33Framebuffer::clear,
    XGL33Framebuffer::resize,
    XGL33Framebuffer::beginRender,
    XGL33Framebuffer::endRender,
    XGL33Framebuffer::present,
    XGL33Framebuffer::getTexture
  }
};
#endif

Renderer *GLRenderer::createGLRenderer(bool gles, Eks::AllocatorBase* alloc)
  {
#ifdef USE_GLEW
  glewInit() GLE_QUIET;
#endif

  const char* ven = (const char *)glGetString(GL_VENDOR) GLE;
  const char* ver = (const char *)glGetString(GL_VERSION) GLE;
  const char* renderer = (const char *)glGetString(GL_RENDERER) GLE;
  const char* glslVer = (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION) GLE;
  qDebug() << "Vendor:" << ven;
  qDebug() << "Renderer:" << renderer GLE;
  qDebug() << "Version:" << ver;
  qDebug() << "GLSL Version:" << glslVer GLE;

  const detail::RendererFunctions *fns = &gl21fns;
  xint32 major = 0;
  if(!gles)
    {
    const char* verPt = ver;
    while(*verPt && *verPt >= '0' && *verPt < '9')
      {
      xint32 num = *verPt - '0';
      major = (major*10) + num;

      ++verPt;
      }

    if(major < 2)
      {
      return nullptr;
      }

#ifdef STANDARD_OPENGL
    if(major >= 3)
      {
      fns = &gl33fns;
      }
#endif
    }
  else
    {
    major = 2;
    }

  GLRendererImpl *r = alloc->create<GLRendererImpl>(*fns, major, alloc);

  GLRendererImpl::setClearColour(r, Colour(0.0f, 0.0f, 0.0f, 1.0f));
  glEnable(GL_DEPTH_TEST) GLE;

  ShaderConstantDataDescription modelDesc[] =
  {
    { "model", ShaderConstantDataDescription::Matrix4x4 },
    { "modelView", ShaderConstantDataDescription::Matrix4x4 },
    { "modelViewProj", ShaderConstantDataDescription::Matrix4x4 },
  };
  ShaderConstantDataDescription viewDesc[] =
  {
    { "view", ShaderConstantDataDescription::Matrix4x4 },
    { "proj", ShaderConstantDataDescription::Matrix4x4 },
  };

  ShaderConstantData::delayedCreate(r->_model, r, modelDesc, X_ARRAY_COUNT(modelDesc));
  ShaderConstantData::delayedCreate(r->_view, r, viewDesc, X_ARRAY_COUNT(viewDesc));

  return r;
  }

void GLRenderer::destroyGLRenderer(Renderer *r, Eks::AllocatorBase* alloc)
  {
  alloc->destroy(GL_REND(r));
  }

//----------------------------------------------------------------------------------------------------------------------
// TEXTURE
//----------------------------------------------------------------------------------------------------------------------
bool XGLTexture2D::init(GLRendererImpl *, xuint32 format, xsize width, xsize height, const void *data)
  {
  _type = GL_TEXTURE_2D;

  size = VectorUI2D((xuint32)width, (xuint32)height);

  glGenTextures(1, &_id) GLE;
  glBindTexture(GL_TEXTURE_2D, _id) GLE;

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST) GLE;
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST) GLE;

  // could also be GL_REPEAT
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE) GLE;
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE) GLE;

  int internalFormatMap[] =
  {
    GL_RGBA,
  #ifdef STANDARD_OPENGL
    GL_DEPTH_COMPONENT24
  #else
  # ifdef X_GLES
    GL_DEPTH_COMPONENT16
  # endif
  #endif
  };
  xCompileTimeAssert(X_ARRAY_COUNT(internalFormatMap) == TextureFormatCount);

  int formatMap[] =
  {
    GL_RGBA,
  #ifdef STANDARD_OPENGL
    GL_DEPTH_COMPONENT
  #else
  # ifdef X_GLES
    GL_DEPTH_COMPONENT
  # endif
  #endif
  };
  xCompileTimeAssert(X_ARRAY_COUNT(formatMap) == TextureFormatCount);

  // 0 at end could be data to unsigned byte...
  glTexImage2D(
    GL_TEXTURE_2D,
    0,
    internalFormatMap[format],
    (GLsizei)width,
    (GLsizei)height,
    0,
    formatMap[format],
    GL_UNSIGNED_BYTE,
    data) GLE;

  glBindTexture(GL_TEXTURE_2D, 0) GLE;

  return true;
  }

XGLTexture2D::~XGLTexture2D()
  {
  clear();
  }

void XGLTexture2D::getInfo(const Renderer *, const Texture2D *tex, Eks::VectorUI2D& v)
  {
  const XGLTexture2D *tImpl = tex->data<XGLTexture2D>();
  v = tImpl->size;
  }

void XGLTexture2D::clear()
  {
  glDeleteTextures(1, &_id) GLE;
  }

//----------------------------------------------------------------------------------------------------------------------
// FRAMEBUFFER
//----------------------------------------------------------------------------------------------------------------------
bool XGL21Framebuffer::init(Renderer *r, TextureFormat cF, TextureFormat dF, xuint32 width, xuint32 height)
  {
  GLRendererImpl *impl = GL_REND(r);

  glGenFramebuffers(1, &_buffer) GLE;
  glBindFramebuffer(GL_FRAMEBUFFER, _buffer) GLE;

  if(!Texture2D::delayedCreate(_textures[FrameBuffer::TextureColour], r, width, height, cF, 0))
    {
    return false;
    }
  XGLTexture2D* c = _textures[FrameBuffer::TextureColour].data<XGLTexture2D>();
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, c->_id, 0) GLE;

  if(!Texture2D::delayedCreate(_textures[FrameBuffer::TextureDepthStencil], r, width, height, dF, 0))
    {
    return false;
    }
  XGLTexture2D* d = _textures[FrameBuffer::TextureDepthStencil].data<XGLTexture2D>();
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, d->_id, 0) GLE;

  glBindFramebuffer(GL_FRAMEBUFFER, 0) GLE;
  return isValid(impl);
  }

bool XGL33Framebuffer::init(Renderer *r, TextureFormat cF, TextureFormat dF, xuint32 width, xuint32 height)
  {
  GLRendererImpl *impl = GL_REND(r);

  glGenFramebuffers(1, &_buffer) GLE;
  glBindFramebuffer(GL_FRAMEBUFFER, _buffer) GLE;

  if(!Texture2D::delayedCreate(_textures[FrameBuffer::TextureColour], r, width, height, cF, 0))
    {
    return false;
    }
  XGLTexture2D* c = _textures[FrameBuffer::TextureColour].data<XGLTexture2D>();
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, c->_id, 0) GLE;

  if(!Texture2D::delayedCreate(_textures[FrameBuffer::TextureDepthStencil], r, width, height, dF, 0))
    {
    return false;
    }
  XGLTexture2D* d = _textures[FrameBuffer::TextureDepthStencil].data<XGLTexture2D>();
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, d->_id, 0) GLE;

  glBindFramebuffer(GL_FRAMEBUFFER, 0) GLE;
  return isValid(impl);
  }

bool XGLFramebuffer::initDefaultBuffer(GLRendererImpl *r)
  {
  _buffer = 0;
  _impl = r;
  return true;
  }

XGL21Framebuffer::~XGL21Framebuffer( )
  {
  if( _buffer )
    {
    glDeleteFramebuffers( 1, &_buffer ) GLE;
    }
  }

XGL33Framebuffer::~XGL33Framebuffer( )
  {
  if( _buffer )
    {
    glDeleteFramebuffers( 1, &_buffer ) GLE;
    }
  }

bool XGL21Framebuffer::isValid(GLRendererImpl *) const
  {
  if(!_buffer)
    {
    return true;
    }

  glBindFramebuffer( GL_FRAMEBUFFER, _buffer ) GLE;
  int status = glCheckFramebufferStatus(GL_FRAMEBUFFER) GLE;

  if( status == GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT )
    {
    qWarning() << "Framebuffer Incomplete attachment";
    }
  else if( status == GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT )
    {
    qWarning() << "Framebuffer Incomplete missing attachment";
    }
  else if( status == GL_FRAMEBUFFER_UNSUPPORTED )
    {
    qWarning() << "Framebuffer unsupported attachment";
    }

  glBindFramebuffer( GL_FRAMEBUFFER, 0 ) GLE;

  return status == GL_FRAMEBUFFER_COMPLETE;
  }

bool XGL33Framebuffer::isValid(GLRendererImpl *) const
  {
  if(_buffer == 0)
    {
    return true;
    }

  glBindFramebuffer( GL_FRAMEBUFFER, _buffer ) GLE;
  int status = glCheckFramebufferStatus(GL_FRAMEBUFFER) GLE;

  if( status == GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT )
    {
    qWarning() << "Framebuffer Incomplete attachment";
    }
  else if( status == GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT )
    {
    qWarning() << "Framebuffer Incomplete missing attachment";
    }
  else if( status == GL_FRAMEBUFFER_UNSUPPORTED )
    {
    qWarning() << "Framebuffer unsupported attachment";
    }

  glBindFramebuffer( GL_FRAMEBUFFER, 0 ) GLE;

  return status == GL_FRAMEBUFFER_COMPLETE;
  }

void XGL21Framebuffer::bind(GLRendererImpl *X_USED_FOR_ASSERTS(r))
  {
  xAssert( isValid(r) );
  glBindFramebuffer( GL_FRAMEBUFFER, _buffer ) GLE;
  }

void XGL33Framebuffer::bind(GLRendererImpl *X_USED_FOR_ASSERTS(r))
  {
  xAssert( isValid(r) );
  glBindFramebuffer( GL_FRAMEBUFFER, _buffer ) GLE;
  }

void XGL21Framebuffer::unbind(GLRendererImpl *)
  {
  glBindFramebuffer( GL_FRAMEBUFFER, 0 ) GLE;
  }

void XGL33Framebuffer::unbind(GLRendererImpl *)
  {
  glBindFramebuffer( GL_FRAMEBUFFER, 0 ) GLE;
  }

#ifdef X_OSX
static void parseGlslError(
    Eks::ParseErrorInterface *ifc,
    Eks::AllocatorBase *alloc,
    const char *data,
    const Eks::String &infoLog)
  {
  if (!ifc)
    {
    return;
    }

  bool error = false;
  xsize line = 0;
  Eks::String msg(alloc);
    
  bool hasMsg = false;
    
  auto idx1 = std::find(infoLog.begin(), infoLog.end(), ':');
  if (idx1 != infoLog.end())
    {
    Eks::String tmp(alloc);
    auto c1 = idx1 - infoLog.begin();
    tmp.mid(infoLog, 0, c1);
    if (tmp == "ERROR")
      {
      error = true;
      }
      
    auto idx2 = std::find(idx1+1, infoLog.end(), ':');
    if (idx2 != infoLog.end())
      {
      auto idx3 = std::find(idx2+1, infoLog.end(), ':');
      if (idx3 != infoLog.end())
        {

        auto c2 = idx2 - infoLog.begin();
        auto c3 = idx3 - infoLog.begin();
        tmp.mid(infoLog, c2+1, c3 - c2 - 1);
        line = tmp.toType<xsize>();

        msg.mid(infoLog, c3+1, infoLog.length() - c3 - 2);
        hasMsg = true;
        }
      }
      
    if (!hasMsg)
      {
      msg.mid(infoLog, c1+1, infoLog.length() - c1 - 2);
      hasMsg = true;
      }
    }
    
  if (!hasMsg)
    {
    ifc->error(X_PARSE_ERROR(msg));
    return;
    }

  auto fn = error ? &Eks::ParseErrorInterface::error : &Eks::ParseErrorInterface::warning;
  (ifc->*fn)(X_PARSE_ERROR(
    Eks::ParseError::FullContext,
    data ? data : "",
    line,
    msg));
  }
#endif

//----------------------------------------------------------------------------------------------------------------------
// SHADER COMPONENT
//----------------------------------------------------------------------------------------------------------------------
bool XGLShaderComponent::init(
    GLRendererImpl *impl,
    xuint32 type,
    const char *data,
    xsize size,
    ParseErrorInterface *ifc)
  {
  _layout = nullptr;

  xuint32 glTypes[] =
  {
    GL_VERTEX_SHADER,
    GL_TESS_CONTROL_SHADER,
    GL_TESS_EVALUATION_SHADER,
    GL_FRAGMENT_SHADER,
    GL_GEOMETRY_SHADER
  };
  xCompileTimeAssert(X_ARRAY_COUNT(glTypes) == ShaderComponent::ShaderComponentCount);

#ifdef USE_GLEW
  xAssert(glCreateShader);
#endif
  _component = glCreateShader(glTypes[type]) GLE;
  if (_component == 0)
    {
    return false;
    }

  int lengths[] =
    {
    (int)strlen(impl->_shaderHeader),
    (int)size,
    };

  const char *strs[] =
    {
    impl->_shaderHeader,
    data,
    };

  glShaderSource(_component, X_ARRAY_COUNT(lengths), strs, lengths) GLE;
  glCompileShader(_component) GLE;

  int infoLogLength = 0;
  glGetShaderiv(_component, GL_INFO_LOG_LENGTH, &infoLogLength) GLE;

  if (infoLogLength > 0)
    {
    TemporaryAllocator alloc(Eks::Core::temporaryAllocator());
    Eks::String infoLog(&alloc);
    infoLog.resize(infoLogLength, '\0');
    int charsWritten  = 0;
    glGetShaderInfoLog(_component, infoLogLength, &charsWritten, infoLog.data()) GLE;

    parseGlslError(ifc, &alloc, data, infoLog);
    }

  int success = 0;
  glGetShaderiv(_component, GL_COMPILE_STATUS, &success) GLE;

  return success == GL_TRUE;
  }

bool XGLShaderComponent::initVertex(
    Renderer *r,
    ShaderComponent *v,
    const ShaderVertexLayoutDescription *vertexDescriptions,
    xsize vertexItemCount,
    ShaderVertexLayout *layout)
  {
  XGLShaderComponent *glS = v->data<XGLShaderComponent>();

  glS->_layout = 0;
  if(layout)
    {
    XGLVertexLayout *glL = layout->create<XGLVertexLayout>();
    glS->_layout = glL;
    xAssert(vertexItemCount > 0);
    xAssert(vertexDescriptions);

    return glL->init1(GL_REND(r), vertexDescriptions, vertexItemCount);
    }

  return true;
  }

//----------------------------------------------------------------------------------------------------------------------
// SHADER DATA
//----------------------------------------------------------------------------------------------------------------------
struct ShaderDataType
  {
  XGL21ShaderData::BindFunction bind;
  xsize size;

  static void bindFloat(xuint32 location, const xuint8 *data8)
    {
    glUniform1fv(location, 1, (const float*)data8) GLE;
    }

  static void bindFloat3(xuint32 location, const xuint8 *data8)
    {
    glUniform3fv(location, 1, (const float*)data8) GLE;
    }

  static void bindFloat4(xuint32 location, const xuint8 *data8)
    {
    glUniform4fv(location, 1, (const float*)data8) GLE;
    }

  static void bindMat4x4(xuint32 location, const xuint8 *data8)
    {
    glUniformMatrix4fv(location, 1, false, (const float*)data8) GLE;
    }
  };

ShaderDataType typeMap[] =
{
  { ShaderDataType::bindFloat, sizeof(float) },
  { ShaderDataType::bindFloat3, sizeof(float) * 3 },
  { ShaderDataType::bindFloat4, sizeof(float) * 4 },
  { ShaderDataType::bindMat4x4, sizeof(float) * 16 },
};
xCompileTimeAssert(X_ARRAY_COUNT(typeMap) == ShaderConstantDataDescription::TypeCount);

bool XGL21ShaderData::init(
    GLRendererImpl *r,
    ShaderConstantDataDescription* desc,
    xsize descCount,
    const void *data)
  {
  _revision = 0;
  _data.allocator() = TypedAllocator<xuint8>(r->_allocator);
  _binders.allocator() = TypedAllocator<Binder>(r->_allocator);

  xsize size = 0;
  _binders.resize(descCount);
  for(xsize i = 0; i < descCount; ++i)
    {
    const ShaderConstantDataDescription &description = desc[i];
    const ShaderDataType &type = typeMap[description.type];

    Binder &b = _binders[i];
    b.name = Eks::String(description.name, r->_allocator);
    b.offset = size;
    b.bind = type.bind;

    size += type.size;
    }

  if(data)
    {
    _data.resizeAndCopy(size, (xuint8*)data);
    }
  else
    {
    _data.resize(size, 0);
    }

  return true;
  }

void XGL21ShaderData::update(Renderer *, ShaderConstantData *constant, void *data)
  {
  XGL21ShaderData* sData = constant->data<XGL21ShaderData>();

  memcpy(sData->_data.data(), data, sData->_data.size());
  ++sData->_revision;
  }

void XGL21ShaderData::bind(xuint32 program, xuint32 index) const
  {
  char str[256];
#ifdef Q_OS_WIN
  xsize pos = sprintf_s(str, X_ARRAY_COUNT(str), "cb%d.", index);
#else
  xsize pos = sprintf(str, "cb%d.", index);
#endif

  const xuint8* data = _data.data();
  xForeach(const Binder &b, _binders)
    {
    xsize strl = b.name.length();
    memcpy(str + pos, b.name.data(), strl);
    str[strl + pos] = '\0';

    xint32 location = glGetUniformLocation(program, str) GLE;
    if(location != -1)
      {
      b.bind(location, data + b.offset);
      }
    }
  }

#ifdef STANDARD_OPENGL
bool XGL33ShaderData::init(
    GLRendererImpl *r,
    ShaderConstantDataDescription *desc,
    xsize descCount,
    const void *data)
  {
  xsize size = 0;
  for(xsize i = 0; i < descCount; ++i)
    {
    const ShaderConstantDataDescription &description = desc[i];
    const ShaderDataType &type = typeMap[description.type];

    size += type.size;
    }

  _size = size;
  xAssert(_size > 0);

  return XGLBuffer::init(r, data, GL_UNIFORM_BUFFER, GL_STREAM_DRAW, size);
  }

void XGL33ShaderData::update(Renderer *, ShaderConstantData *constant, void *data)
  {
  XGL33ShaderData *c = constant->data<XGL33ShaderData>();

  glBindBuffer(GL_UNIFORM_BUFFER, c->_buffer) GLE;
  glBufferData(GL_UNIFORM_BUFFER, c->_size, data, GL_STREAM_DRAW) GLE;
  glBindBuffer(GL_UNIFORM_BUFFER, 0) GLE;
  }

void XGL33ShaderData::bind(xuint32 program, xuint32 index) const
  {
  xAssert(index < 32);
  char str[64];
#ifdef Q_OS_WIN
  sprintf_s(str, X_ARRAY_COUNT(str), "cb%d", index);
#else
  sprintf(str, "cb%d", index);
#endif

  GLuint blockIndex = glGetUniformBlockIndex(program, str) GLE;
  if(blockIndex != GL_INVALID_INDEX)
    {
    glUniformBlockBinding(program, blockIndex, index) GLE;
    }

  glBindBufferBase(GL_UNIFORM_BUFFER, index, _buffer) GLE;
  }
#endif

//--------------------------	--------------------------------------------------------------------------------------------
// SHADER
//----------------------------------------------------------------------------------------------------------------------
XGLShader::~XGLShader()
  {
  glDeleteProgram(shader);
  }

bool XGLShader::init(
    GLRendererImpl *impl,
    ShaderComponent **v,
    xsize shaderCount,
    const char **outputs,
    xsize outputCount,
    ParseErrorInterface *ifc)
  {
  _buffers.allocator() = TypedAllocator<Buffer>(impl->_allocator);
  maxSetupResources = 0;
  shader = glCreateProgram();
  for (xsize i = 0; i < shaderCount; ++i)
    {
    XGLShaderComponent *comp = v[i]->data<XGLShaderComponent>();
    glAttachShader(shader, comp->_component) GLE;
    }

  for(xsize i = 0; i < outputCount; ++i)
    {
    if(outputs[i])
      {
      glBindFragDataLocation(shader, i, outputs[i]) GLE;
      }
    }


  for(xsize i = 0; i < shaderCount; ++i)
    {
    XGLShaderComponent *comp = v[i]->data<XGLShaderComponent>();
    if(comp->_layout)
      {
      if(!comp->_layout->init2(impl, this))
        {
        return false;
        }
      }
    }

  glLinkProgram(shader) GLE;

  int infologLength = 0;

  glGetProgramiv(shader, GL_INFO_LOG_LENGTH, &infologLength) GLE;

  if (infologLength > 0)
    {
    Eks::String infoLog(impl->_allocator);
    infoLog.resize(infologLength, '\0');
    int charsWritten  = 0;
    glGetProgramInfoLog(shader, infologLength, &charsWritten, infoLog.data());

    Eks::TemporaryAllocator alloc(Eks::Core::temporaryAllocator());
    parseGlslError(ifc, &alloc, nullptr, infoLog);
    }

  int success = 0;
  glGetProgramiv(shader, GL_LINK_STATUS, &success) GLE;
  if(!success)
    {
    return false;
    }

  return true;
  }

void XGLShader::bindDumb(Renderer *ren)
  {
  GLRendererImpl* r = GL_REND(ren);
  r->_currentShader = nullptr;
  r->_vertexLayout = nullptr;
  glUseProgram(shader);
  }

void XGLShader::bind(Renderer *ren, const Shader *shader, const ShaderVertexLayout *layout)
  {
  GLRendererImpl* r = GL_REND(ren);
  if( shader &&
      ( r->_currentShader == 0 || r->_currentShader != shader || r->_vertexLayout != layout) )
    {
    r->_currentShader = const_cast<Shader *>(shader);
    r->_vertexLayout = const_cast<ShaderVertexLayout *>(layout);
    XGLShader* shaderInt = r->_currentShader->data<XGLShader>();
    XGLVertexLayout* shaderVL = r->_currentShader->data<XGLVertexLayout>();
    (void)shaderVL;

    glUseProgram(shaderInt->shader) GLE;
    }
  else if(shader == 0 && r->_currentShader != 0)
    {
    glUseProgram(0);
    r->_currentShader = 0;
    r->_vertexLayout = 0;
    }
  }

#if 0
void XGLShader::setConstantBuffers(
    Renderer *,
    Shader *shader,
    xsize index,
    xsize count,
    const ShaderConstantData * const* data)
  {
  XGLShader* shaderImpl = shader->data<XGLShader>();
  for(xsize i = 0; i < count; ++i)
    {
    xsize blockIndex = i + index + GLRendererImpl::ConstantBufferIndexOffset;
    const XGL21ShaderData* sImpl = data[i]->data<XGL21ShaderData>();

    glUniformBlockBinding(shaderImpl->shader, blockIndex, blockIndex);
    sImpl->bind(blockIndex);
    }
  }
#endif

void XGLShader::setConstantBuffersInternal21(
    Renderer *r,
    Shader *s,
    xsize index,
    xsize count,
    const ShaderConstantData * const* data)
  {
  XGLShader* shader = s->data<XGLShader>();

  if(GL_REND(r)->_currentShader != s)
    {
    glUseProgram(shader->shader);
    GL_REND(r)->_currentShader = 0;
    }

  if(count > shader->_buffers.size())
    {
    shader->_buffers.resize(count, Buffer());
    }

  for(xuint32 i = 0; i < (xuint32)count; ++i)
    {
    const ShaderConstantData *cb = data[i];
    const XGL21ShaderData* cbImpl = cb->data<XGL21ShaderData>();

    Buffer &buf = shader->_buffers[i];

    if(buf.data != cbImpl || buf.revision != cbImpl->_revision)
      {
      cbImpl->bind(shader->shader, i + (xuint32)index);
      buf.revision = cbImpl->_revision;
      }
    }
  }

void XGLShader::setConstantBuffersInternal33(
    Renderer *r,
    Shader *s,
    xsize index,
    xsize count,
    const ShaderConstantData * const* data)
  {
  XGLShader* shader = s->data<XGLShader>();

  if(GL_REND(r)->_currentShader != s)
    {
    glUseProgram(shader->shader);
    GL_REND(r)->_currentShader = 0;
    }

  for(xuint32 i = 0; i < (xuint32)count; ++i)
    {
    const ShaderConstantData *cb = data[i];
    const XGL33ShaderData* cbImpl = cb->data<XGL33ShaderData>();

    cbImpl->bind(shader->shader, i + (xuint32)index);
    }
  }

void XGLShader::setResources21(
    Renderer *r,
    Shader *s,
    xsize index,
    xsize count,
    const Resource * const* data)
  {
  XGLShader* shader = s->data<XGLShader>();
  shader->bindDumb(r);
  for(GLuint i = shader->maxSetupResources; i < (GLuint)count; ++i)
    {
    char str[256];
  #ifdef Q_OS_WIN
    sprintf_s(str, X_ARRAY_COUNT(str), "rsc%d", (int)index);
  #else
    sprintf(str, "rsc%d", (int)index);
  #endif

    xint32 location = glGetUniformLocation(shader->shader, str) GLE;

    if(location != -1)
      {
      glUniform1i(location, i+index) GLE;
      }
    }

  for(GLuint i = 0; i < (GLuint)count; ++i)
    {
    const Resource *rsc = data[i];
    const XGLShaderResource* rscImpl = rsc->data<XGLShaderResource>();
    rscImpl->bindResource(i+(GLuint)index);
    }
  }

//----------------------------------------------------------------------------------------------------------------------
// BUFFER
//----------------------------------------------------------------------------------------------------------------------
bool XGLBuffer::init( GLRendererImpl *, const void *data, xuint32 type, xuint32 renderType, xsize size)
  {
  glGenBuffers(1, &_buffer);

  glBindBuffer(type, _buffer) GLE;
  glBufferData(type, size, data, renderType) GLE;
  glBindBuffer(type, 0) GLE;

  return true;
  }


XGLBuffer::~XGLBuffer( )
  {
  glDeleteBuffers(1, &_buffer) GLE;
  }

//----------------------------------------------------------------------------------------------------------------------
// INDEX GEOMETRY CACHE
//----------------------------------------------------------------------------------------------------------------------
bool XGLIndexGeometryCache::init(GLRendererImpl *r, const void *data, IndexGeometry::Type type, xsize elementCount)
  {
  struct Type
    {
    xuint32 type;
    xuint32 size;
    };

  Type typeMap[] =
  {
    { GL_UNSIGNED_SHORT, sizeof(xuint16) }
  };
  xCompileTimeAssert(IndexGeometry::TypeCount == X_ARRAY_COUNT(typeMap));

  _indexType = typeMap[type].type;
  _indexCount = (GLuint)elementCount;

  xsize dataSize = elementCount * typeMap[type].size;
  return XGLBuffer::init(r, data, GL_ELEMENT_ARRAY_BUFFER, GL_STATIC_DRAW, dataSize);
  }

//----------------------------------------------------------------------------------------------------------------------
// GEOMETRY CACHE
//----------------------------------------------------------------------------------------------------------------------

bool XGLGeometryCache::init(GLRendererImpl *r, const void *data, xsize elementSize, xsize elementCount)
  {
  _vao = 0;
  _linkedLayout = nullptr;
  _linkedIndices = nullptr;

  xsize dataSize = elementSize * elementCount;
  _elementCount = (GLuint)elementCount;
  _elementSize = (GLuint)elementSize;
  return XGLBuffer::init(r, data, GL_ARRAY_BUFFER, GL_STATIC_DRAW, dataSize);
  }

}

#endif
