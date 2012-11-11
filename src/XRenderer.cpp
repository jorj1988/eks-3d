#include "XRenderer.h"
#include "XShape.h"

XRenderer::XRenderer( ) : _renderFlags(0)
  {
  }

XRenderer::~XRenderer( )
  {
  }

/*void XRenderer::drawShape( XShape &shape )
  {
  int prevRenderFlags( renderFlags() );
  setRenderFlags( shape.renderFlags() );
  setShader( shape.shader( ) );
  drawGeometry( shape.geometry( ) );
  setRenderFlags( prevRenderFlags );
  }

void XRenderer::drawShapes( XShapeList &list )
  {
  if(list.isEmpty())
    {
    return;
    }

  //int prevRenderFlags( renderFlags() );
  XShapeList::iterator it = list.begin();
  XShapeList::iterator end = list.end();
  for(;it != end; ++it)
    {
    setRenderFlags( it->renderFlags() );
    setShader( it->constShader( ) );
    drawGeometry( it->geometry( ) );
    }
  //setRenderFlags( prevRenderFlags );
  }*/

void XRenderer::setRenderFlags( int flags )
  {
  for( unsigned int x=0; x<sizeof(int)*8; x++ )
    {
    int flag = 1 << x;
    bool newHasFlag = (flags & flag) != 0;

    if(newHasFlag != _renderFlags.hasAnyFlags(flag))
      {
      if(newHasFlag)
        {
        enableRenderFlag( (RenderFlags)(1 << x) );
        }
      else
        {
        disableRenderFlag( (RenderFlags)(1 << x) );
        }
      }
    }
  _renderFlags = flags;
  }

int XRenderer::renderFlags() const
  {
  return *_renderFlags;
  }
