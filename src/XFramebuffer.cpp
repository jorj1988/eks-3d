#include "XFramebuffer.h"
#include "XRenderer.h"

namespace Eks
{

FrameBuffer::FrameBuffer(
    Renderer *r,
    xuint32 width,
    xuint32 height,
    TextureFormat colour,
    TextureFormat dsF)
  {
  if(r)
    {
    delayedCreate(*this, r, width, height, colour, dsF);
    }
  }

FrameBuffer::~FrameBuffer()
  {
  if(_renderer)
    {
    _renderer->functions().destroy.framebuffer(_renderer, this);
    }
  }

bool FrameBuffer::delayedCreate(
    FrameBuffer &ths,
    Renderer *r,
    xuint32 width,
    xuint32 height,
    TextureFormat colour,
    TextureFormat dsF)
  {
  xAssert(width > 0);
  xAssert(height > 0);
  ths._renderer = r;
  return r->functions().create.framebuffer(r, &ths, width, height, colour, dsF);
  }

ScreenFrameBuffer::ScreenFrameBuffer(Eks::Renderer *r)
  {
  if(r)
    {
    delayedCreate(*this, r);
    }
  }

ScreenFrameBuffer::~ScreenFrameBuffer()
  {
  }

bool ScreenFrameBuffer::delayedCreate(
    ScreenFrameBuffer &ths,
    Renderer *r)
  {
  ths._renderer = r;
  return r->functions().create.viewport(r, &ths);
  }

void ScreenFrameBuffer::present(bool *deviceLost)
  {
  return _renderer->functions().frame.present(_renderer, this, deviceLost);
  }

bool ScreenFrameBuffer::resize(xuint32 w, xuint32 h, Rotation rotation)
  {
  xAssert(_renderer)
  return _renderer->functions().frame.resize(_renderer, this, w, h, rotation);
  }

FrameBufferRenderFrame::FrameBufferRenderFrame(Renderer *r, FrameBuffer *buffer)
    : _framebuffer(buffer),
    _renderer(r)
  {
  _renderer->functions().frame.begin(_renderer, _framebuffer);
  }

FrameBufferRenderFrame::~FrameBufferRenderFrame()
  {
  _renderer->functions().frame.end(_renderer, _framebuffer);
  }
}
