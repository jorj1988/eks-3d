#ifndef XFRAMEBUFFER_H
#define XFRAMEBUFFER_H

#include "X3DGlobal.h"
#include "Utilities/XProperty.h"
#include "Utilities/XPrivateImpl.h"

namespace Eks
{

class Texture2D;
class Renderer;
class FrameBufferRenderFrame;

class EKS3D_EXPORT FrameBuffer : public PrivateImpl<sizeof(void *) * 16>
  {
public:
  typedef FrameBufferRenderFrame RenderFrame;

  enum TextureId
    {
    TextureColour,
    TextureDepthStencil,

    TextureIdCount
    };

  FrameBuffer(
    Renderer *r = 0,
    xuint32 width = 0,
    xuint32 height = 0,
    TextureFormat colour = Eks::Rgba8,
    TextureFormat dsF = Eks::Depth24);
  ~FrameBuffer();

  static bool delayedCreate(
    FrameBuffer &ths,
    Renderer *r,
    xuint32 width,
    xuint32 height,
    TextureFormat colour = Eks::Rgba8,
    TextureFormat dsF = Eks::Depth24);

  enum ClearMode
    {
    ClearColour = 1,
    ClearDepth = 2
    };

  void clear(xuint32 mode);

  Texture2D *getTexture(TextureId id);

protected:
  Renderer *_renderer;
  };

class EKS3D_EXPORT ScreenFrameBuffer : public FrameBuffer
  {
public:
  ScreenFrameBuffer(Eks::Renderer *r);
  ~ScreenFrameBuffer();

  static bool delayedCreate(
    ScreenFrameBuffer &ths,
    Renderer *r);

  enum Rotation
    {
    RotateNone,
    Rotate90,
    Rotate180,
    Rotate270
    };

  void present(bool *deviceLost);
  bool resize(xuint32 w, xuint32 h, Rotation rotation);

private:
  // trying to hide the parent method
  static void delayedCreate() { }

  X_DISABLE_COPY(ScreenFrameBuffer)
  };

class EKS3D_EXPORT FrameBufferRenderFrame
  {
public:
  FrameBufferRenderFrame(Renderer *r, FrameBuffer *buffer);
  ~FrameBufferRenderFrame();

private:
  X_DISABLE_COPY(FrameBufferRenderFrame)

  FrameBuffer *_framebuffer;
  Renderer *_renderer;
  };
}

#include "XRenderer.h"

namespace Eks
{

inline void FrameBuffer::clear(xuint32 mode)
  {
  xAssert(_renderer);
  _renderer->functions().frame.clear(_renderer, this, mode);
  }

inline Texture2D *FrameBuffer::getTexture(TextureId tex)
  {
  return _renderer->functions().frame.getTexture(_renderer, this, tex);
  }

}

#endif // XFRAMEBUFFER_H
