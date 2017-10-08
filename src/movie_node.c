/******************************************************************************
* libMOVIE Software License v1.0
*
* Copyright (c) 2016-2017, Levchenko Yuriy <irov13@mail.ru>
* All rights reserved.
*
* You are granted a perpetual, non-exclusive, non-sublicensable, and
* non-transferable license to use, install, execute, and perform the libMOVIE
* software and derivative works solely for personal or internal
* use. Without the written permission of Levchenko Yuriy, you may not (a) modify, translate,
* adapt, or develop new applications using the libMOVIE or otherwise
* create derivative works or improvements of the libMOVIE or (b) remove,
* delete, alter, or obscure any trademarks or any copyright, trademark, patent,
* or other intellectual property or proprietary rights notices on or in the
* Software, including any copy thereof. Redistributions in binary or source
* form must include this license and terms.
*
* THIS SOFTWARE IS PROVIDED BY LEVCHENKO YURIY "AS IS" AND ANY EXPRESS OR
* IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
* EVENT SHALL LEVCHENKO YURIY BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
* SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
* PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES, BUSINESS INTERRUPTION,
* OR LOSS OF USE, DATA, OR PROFITS) HOWEVER CAUSED AND ON ANY THEORY OF
* LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
* OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
* ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*****************************************************************************/

#	include "movie/movie_node.h"
#	include "movie/movie_resource.h"

#	include "movie_transformation.h"
#	include "movie_memory.h"
#	include "movie_math.h"

#	include "movie_struct.h"

//////////////////////////////////////////////////////////////////////////
typedef enum
{
    AE_MOVIE_NODE_ANIMATE_STATIC,
    AE_MOVIE_NODE_ANIMATE_BEGIN,
    AE_MOVIE_NODE_ANIMATE_PROCESS,
    AE_MOVIE_NODE_ANIMATE_END,
    __AE_MOVIE_NODE_ANIMATE_STATES__
} aeMovieNodeAnimationStateEnum;
//////////////////////////////////////////////////////////////////////////
static ae_uint32_t __get_composition_update_revision( const aeMovieComposition * _composition )
{
    ae_uint32_t update_revision = *_composition->update_revision;

    return update_revision;
}
//////////////////////////////////////////////////////////////////////////
static ae_uint32_t __inc_composition_update_revision( const aeMovieComposition * _composition )
{
    ae_uint32_t update_revision = ++(*_composition->update_revision);

    return update_revision;
}
//////////////////////////////////////////////////////////////////////////
static void __make_mesh_vertices( const aeMovieMesh * _mesh, const ae_matrix4_t _matrix, aeMovieRenderMesh * _render )
{
    _render->vertexCount = _mesh->vertex_count;
    _render->indexCount = _mesh->indices_count;

    ae_uint32_t vertex_count = _mesh->vertex_count;

    ae_uint32_t i = 0;
    for( ; i != vertex_count; ++i )
    {
        ae_mul_v3_v2_m4_r( _render->position[i], _mesh->positions[i], _matrix );
    }

    _render->uv = _mesh->uvs;
    _render->indices = _mesh->indices;
}
//////////////////////////////////////////////////////////////////////////
static void __make_layer_sprite_vertices( const aeMovieInstance * _instance, ae_float_t _offset_x, ae_float_t _offset_y, ae_float_t _width, ae_float_t _height, const ae_matrix4_t _matrix, const ae_vector2_t * _uv, aeMovieRenderMesh * _render )
{
    ae_vector2_t v_position[4];

    ae_float_t * v = &v_position[0][0];

    *v++ = _offset_x + _width * 0.f;
    *v++ = _offset_y + _height * 0.f;
    *v++ = _offset_x + _width * 1.f;
    *v++ = _offset_y + _height * 0.f;
    *v++ = _offset_x + _width * 1.f;
    *v++ = _offset_y + _height * 1.f;
    *v++ = _offset_x + _width * 0.f;
    *v++ = _offset_y + _height * 1.f;

    _render->vertexCount = 4;
    _render->indexCount = 6;

    ae_mul_v3_v2_m4_r( _render->position[0], v_position[0], _matrix );
    ae_mul_v3_v2_m4_r( _render->position[1], v_position[1], _matrix );
    ae_mul_v3_v2_m4_r( _render->position[2], v_position[2], _matrix );
    ae_mul_v3_v2_m4_r( _render->position[3], v_position[3], _matrix );

    _render->uv = _uv;
    _render->indices = _instance->sprite_indices;
}
//////////////////////////////////////////////////////////////////////////
static void __make_layer_mesh_vertices( const aeMovieLayerMesh * _layerMesh, ae_uint32_t _frame, const ae_matrix4_t _matrix, aeMovieRenderMesh * _render )
{
    const aeMovieMesh * mesh = (_layerMesh->immutable == AE_TRUE) ? &_layerMesh->immutable_mesh : (_layerMesh->meshes + _frame);

    __make_mesh_vertices( mesh, _matrix, _render );
}
//////////////////////////////////////////////////////////////////////////
static ae_float_t __bezier_point( ae_float_t a, ae_float_t b, ae_float_t c, ae_float_t d, ae_float_t t )
{
    ae_float_t t2 = t * t;
    ae_float_t t3 = t2 * t;

    ae_float_t ti = 1.f - t;
    ae_float_t ti2 = ti * ti;
    ae_float_t ti3 = ti2 * ti;

    return a * ti3 + 3.f * b * t * ti2 + 3.f * c * t2 * ti + d * t3;
}
//////////////////////////////////////////////////////////////////////////
static ae_float_t __bezier_warp_x( const aeMovieBezierWarp * _bezierWarp, ae_float_t _u, ae_float_t _v )
{
    const ae_vector2_t * c = _bezierWarp->corners;
    const ae_vector2_t * b = _bezierWarp->beziers;

    ae_float_t bx = __bezier_point( c[0][0], b[0][0], b[7][0], c[3][0], _v );
    ae_float_t ex = __bezier_point( c[1][0], b[3][0], b[4][0], c[2][0], _v );

    ae_float_t mx0 = (b[1][0] - b[6][0]) * (1.f - _v) + b[6][0];
    ae_float_t mx1 = (b[2][0] - b[5][0]) * (1.f - _v) + b[5][0];

    ae_float_t x = __bezier_point( bx, mx0, mx1, ex, _u );

    return x;
}
//////////////////////////////////////////////////////////////////////////
static ae_float_t __bezier_warp_y( const aeMovieBezierWarp * _bezierWarp, ae_float_t _u, ae_float_t _v )
{
    const ae_vector2_t * c = _bezierWarp->corners;
    const ae_vector2_t * b = _bezierWarp->beziers;

    ae_float_t by = __bezier_point( c[0][1], b[0][1], b[7][1], c[3][1], _v );
    ae_float_t ey = __bezier_point( c[1][1], b[3][1], b[4][1], c[2][1], _v );

    ae_float_t my0 = (b[1][1] - b[6][1]) * (1.f - _v) + b[6][1];
    ae_float_t my1 = (b[2][1] - b[5][1]) * (1.f - _v) + b[5][1];

    ae_float_t y = __bezier_point( by, my0, my1, ey, _u );

    return y;
}
//////////////////////////////////////////////////////////////////////////
static void __make_bezier_warp_vertices( const aeMovieInstance * _instance, const aeMovieBezierWarp * _bezierWarp, const ae_matrix4_t _matrix, aeMovieRenderMesh * _render )
{
    _render->vertexCount = AE_MOVIE_BEZIER_WARP_GRID_VERTEX_COUNT;
    _render->indexCount = AE_MOVIE_BEZIER_WARP_GRID_INDICES_COUNT;

    ae_float_t du = 0.f;
    ae_float_t dv = 0.f;

    ae_vector3_t * positions = _render->position;

    ae_uint32_t v = 0;
    for( ; v != AE_MOVIE_BEZIER_WARP_GRID; ++v )
    {
        ae_uint32_t u = 0;
        for( ; u != AE_MOVIE_BEZIER_WARP_GRID; ++u )
        {
            const ae_float_t x = __bezier_warp_x( _bezierWarp, du, dv );
            const ae_float_t y = __bezier_warp_y( _bezierWarp, du, dv );

            ae_vector2_t position;
            position[0] = x;
            position[1] = y;

            ae_mul_v3_v2_m4_r( *positions++, position, _matrix );

            du += ae_movie_bezier_warp_grid_invf;
        }

        du = 0.f;
        dv += ae_movie_bezier_warp_grid_invf;
    }

    _render->uv = _instance->bezier_warp_uv;
    _render->indices = _instance->bezier_warp_indices;
}
//////////////////////////////////////////////////////////////////////////
static void __make_layer_bezier_warp_vertices( const aeMovieInstance * _instance, const aeMovieLayerBezierWarp * _bezierWarp, ae_uint32_t _frame, ae_float_t _t, const ae_matrix4_t _matrix, aeMovieRenderMesh * _render )
{
    if( _bezierWarp->immutable == AE_TRUE )
    {
        __make_bezier_warp_vertices( _instance, &_bezierWarp->immutable_bezier_warp, _matrix, _render );
    }
    else
    {
        const aeMovieBezierWarp * bezier_warp_frame_current = _bezierWarp->bezier_warps + _frame + 0;
        const aeMovieBezierWarp * bezier_warp_frame_next = _bezierWarp->bezier_warps + _frame + 1;

        const ae_vector2_t * current_corners = bezier_warp_frame_current->corners;
        const ae_vector2_t * next_corners = bezier_warp_frame_next->corners;

        aeMovieBezierWarp bezierWarp;

        ae_linerp_f2( bezierWarp.corners[0], current_corners[0], next_corners[0], _t );
        ae_linerp_f2( bezierWarp.corners[1], current_corners[1], next_corners[1], _t );
        ae_linerp_f2( bezierWarp.corners[2], current_corners[2], next_corners[2], _t );
        ae_linerp_f2( bezierWarp.corners[3], current_corners[3], next_corners[3], _t );

        const ae_vector2_t * current_beziers = bezier_warp_frame_current->beziers;
        const ae_vector2_t * next_beziers = bezier_warp_frame_next->beziers;

        ae_linerp_f2( bezierWarp.beziers[0], current_beziers[0], next_beziers[0], _t );
        ae_linerp_f2( bezierWarp.beziers[1], current_beziers[1], next_beziers[1], _t );
        ae_linerp_f2( bezierWarp.beziers[2], current_beziers[2], next_beziers[2], _t );
        ae_linerp_f2( bezierWarp.beziers[3], current_beziers[3], next_beziers[3], _t );
        ae_linerp_f2( bezierWarp.beziers[4], current_beziers[4], next_beziers[4], _t );
        ae_linerp_f2( bezierWarp.beziers[5], current_beziers[5], next_beziers[5], _t );
        ae_linerp_f2( bezierWarp.beziers[6], current_beziers[6], next_beziers[6], _t );
        ae_linerp_f2( bezierWarp.beziers[7], current_beziers[7], next_beziers[7], _t );

        __make_bezier_warp_vertices( _instance, &bezierWarp, _matrix, _render );
    }
}
//////////////////////////////////////////////////////////////////////////
static ae_uint32_t __compute_movie_node_frame( const aeMovieNode * _node, ae_bool_t _interpolate, ae_float_t * _t )
{
    const aeMovieLayerData * layer = _node->layer;

    const aeMovieCompositionData * composition_data = layer->composition_data;

    ae_float_t frameDurationInv = composition_data->frameDurationInv;

    ae_uint32_t frame;

    if( layer->reverse_time == AE_TRUE )
    {
        ae_float_t frame_time = (_node->out_time - _node->in_time - _node->current_time) * frameDurationInv;

        frame = (ae_uint32_t)frame_time;

        if( _interpolate == AE_TRUE )
        {
            *_t = frame_time - (ae_float_t)frame;
        }
    }
    else
    {
        ae_float_t frame_time = (_node->current_time) * frameDurationInv;

        frame = (ae_uint32_t)frame_time;

        if( _interpolate == AE_TRUE )
        {
            *_t = frame_time - (ae_float_t)frame;
        }
    }

    return frame;
}
//////////////////////////////////////////////////////////////////////////
static void __compute_movie_node( const aeMovieComposition * _composition, const aeMovieNode * _node, aeMovieRenderMesh * _render, ae_bool_t _interpolate, ae_bool_t _trackmatte )
{
    const aeMovieData * movie_data = _composition->movie_data;
    const aeMovieInstance * instance = movie_data->instance;
    const aeMovieLayerData * layer = _node->layer;
    const aeMovieResource * resource = layer->resource;

    aeMovieLayerTypeEnum layer_type = layer->type;

    _render->layer_type = layer_type;

    _render->blend_mode = _node->blend_mode;

    if( resource != AE_NULL )
    {
        _render->resource_type = resource->type;
        _render->resource_data = resource->data;
    }
    else
    {
        _render->resource_type = AE_MOVIE_RESOURCE_NONE;
        _render->resource_data = AE_NULL;
    }

    _render->camera_data = _node->camera_data;
    _render->element_data = _node->element_data;
    _render->shader_data = _node->shader_data;

    if( _node->track_matte_node != AE_NULL && _node->track_matte_node->active == AE_TRUE )
    {
        _render->track_matte_data = _node->track_matte_node->track_matte_data;
    }
    else
    {
        _render->track_matte_data = AE_NULL;
    }

    ae_float_t t_frame = 0.f;
    ae_uint32_t frame = __compute_movie_node_frame( _node, _interpolate, &t_frame );

    switch( layer_type )
    {
    case AE_MOVIE_LAYER_TYPE_SHAPE:
        {
            __make_layer_mesh_vertices( layer->mesh, frame, _node->matrix, _render );

            _render->r = _node->r;
            _render->g = _node->g;
            _render->b = _node->b;
            _render->a = _node->opacity;
        }break;
    case AE_MOVIE_LAYER_TYPE_SOLID:
        {
            aeMovieResourceSolid * resource_solid = (aeMovieResourceSolid *)resource;

            if( layer->mesh != AE_NULL )
            {
                __make_layer_mesh_vertices( layer->mesh, frame, _node->matrix, _render );
            }
            else if( layer->bezier_warp != AE_NULL )
            {
                __make_layer_bezier_warp_vertices( instance, layer->bezier_warp, frame, t_frame, _node->matrix, _render );
            }
            else
            {
                ae_float_t width = resource_solid->width;
                ae_float_t height = resource_solid->height;

                __make_layer_sprite_vertices( instance, 0.f, 0.f, width, height, _node->matrix, instance->sprite_uv, _render );
            }

            _render->r = _node->r * resource_solid->r;
            _render->g = _node->g * resource_solid->g;
            _render->b = _node->b * resource_solid->b;
            _render->a = _node->opacity;
        }break;
    case AE_MOVIE_LAYER_TYPE_SEQUENCE:
        {
            aeMovieResourceSequence * resource_sequence = (aeMovieResourceSequence *)resource;

            ae_uint32_t frame_sequence;

            if( layer->timeremap != AE_NULL )
            {
                ae_float_t time = layer->timeremap->times[frame];

                frame_sequence = (ae_uint32_t)(time * resource_sequence->frameDurationInv);
            }
            else
            {
                if( layer->reverse_time == AE_TRUE )
                {
                    frame_sequence = (ae_uint32_t)((_node->out_time - _node->in_time - (layer->start_time + _node->current_time)) * resource_sequence->frameDurationInv);
                }
                else
                {
                    frame_sequence = (ae_uint32_t)((layer->start_time + _node->current_time) * resource_sequence->frameDurationInv);
                }
            }

            frame_sequence %= resource_sequence->image_count;

            const aeMovieResourceImage * resource_image = resource_sequence->images[frame_sequence];

            _render->resource_type = resource_image->type;
            _render->resource_data = resource_image->data;

            if( layer->mesh != AE_NULL )
            {
                __make_layer_mesh_vertices( layer->mesh, frame, _node->matrix, _render );
            }
            else if( layer->bezier_warp != AE_NULL )
            {
                __make_layer_bezier_warp_vertices( instance, layer->bezier_warp, frame, t_frame, _node->matrix, _render );
            }
            else if( resource_image->mesh != AE_NULL && _trackmatte == AE_FALSE )
            {
                __make_mesh_vertices( resource_image->mesh, _node->matrix, _render );
            }
            else
            {
                ae_float_t offset_x = resource_image->offset_x;
                ae_float_t offset_y = resource_image->offset_y;

                ae_float_t width = resource_image->trim_width;
                ae_float_t height = resource_image->trim_height;

                __make_layer_sprite_vertices( instance, offset_x, offset_y, width, height, _node->matrix, resource_image->uv, _render );
            }

            _render->r = _node->r;
            _render->g = _node->g;
            _render->b = _node->b;
            _render->a = _node->opacity;
        }break;
    case AE_MOVIE_LAYER_TYPE_VIDEO:
        {
            aeMovieResourceVideo * resource_video = (aeMovieResourceVideo *)resource;

            if( layer->mesh != AE_NULL )
            {
                __make_layer_mesh_vertices( layer->mesh, frame, _node->matrix, _render );
            }
            else if( layer->bezier_warp != AE_NULL )
            {
                __make_layer_bezier_warp_vertices( instance, layer->bezier_warp, frame, t_frame, _node->matrix, _render );
            }
            else
            {
                ae_float_t width = resource_video->width;
                ae_float_t height = resource_video->height;

                __make_layer_sprite_vertices( instance, 0.f, 0.f, width, height, _node->matrix, instance->sprite_uv, _render );
            }

            _render->r = _node->r;
            _render->g = _node->g;
            _render->b = _node->b;
            _render->a = _node->opacity;
        }break;
    case AE_MOVIE_LAYER_TYPE_IMAGE:
        {
            aeMovieResourceImage * resource_image = (aeMovieResourceImage *)resource;

            if( layer->mesh != AE_NULL )
            {
                __make_layer_mesh_vertices( layer->mesh, frame, _node->matrix, _render );
            }
            else if( layer->bezier_warp != AE_NULL )
            {
                __make_layer_bezier_warp_vertices( instance, layer->bezier_warp, frame, t_frame, _node->matrix, _render );
            }
            else if( resource_image->mesh != AE_NULL && _trackmatte == AE_FALSE )
            {
                __make_mesh_vertices( resource_image->mesh, _node->matrix, _render );
            }
            else
            {
                ae_float_t offset_x = resource_image->offset_x;
                ae_float_t offset_y = resource_image->offset_y;

                ae_float_t width = resource_image->trim_width;
                ae_float_t height = resource_image->trim_height;

                __make_layer_sprite_vertices( instance, offset_x, offset_y, width, height, _node->matrix, resource_image->uv, _render );
            }

            _render->r = _node->r;
            _render->g = _node->g;
            _render->b = _node->b;
            _render->a = _node->opacity;
        }break;
    default:
        {
            _render->vertexCount = 0;
            _render->indexCount = 0;

            _render->r = _node->r;
            _render->g = _node->g;
            _render->b = _node->b;
            _render->a = _node->opacity;
        }break;
    }
}
//////////////////////////////////////////////////////////////////////////
static aeMovieNode * __find_node_by_layer( aeMovieNode * _nodes, ae_uint32_t _begin, ae_uint32_t _end, const aeMovieLayerData * _layer )
{
    aeMovieNode * it_node = _nodes + _begin;
    aeMovieNode * it_node_end = _nodes + _end;
    for( ; it_node != it_node_end; ++it_node )
    {
        aeMovieNode * node = it_node;

        const aeMovieLayerData * node_layer = node->layer;

        if( node_layer == _layer )
        {
            return node;
        }
    }

    return AE_NULL;
}
//////////////////////////////////////////////////////////////////////////
static const aeMovieLayerData * __find_layer_by_index( const aeMovieCompositionData * _compositionData, ae_uint32_t _index )
{
    const aeMovieLayerData * it_layer = _compositionData->layers;
    const aeMovieLayerData * it_layer_end = _compositionData->layers + _compositionData->layer_count;
    for( ; it_layer != it_layer_end; ++it_layer )
    {
        const aeMovieLayerData * layer = it_layer;

        ae_uint32_t layer_index = layer->index;

        if( layer_index == _index )
        {
            return layer;
        }
    }

    return AE_NULL;
}
//////////////////////////////////////////////////////////////////////////
static ae_float_t __compute_movie_property_value( const struct aeMoviePropertyValue * _property, ae_uint32_t _frame, ae_bool_t _interpolate, ae_float_t t )
{
    if( _property->immutable == AE_TRUE )
    {
        return _property->immutable_value;
    }

    if( _interpolate == AE_FALSE )
    {
        ae_float_t value = _property->values[_frame];

        return value;
    }

    ae_float_t value0 = _property->values[_frame + 0];
    ae_float_t value1 = _property->values[_frame + 1];

    ae_float_t valuef = ae_linerp_f1( value0, value1, t );

    return valuef;
}
//////////////////////////////////////////////////////////////////////////
static ae_float_t __compute_movie_property_color_r( const struct aeMoviePropertyColor * _property, ae_uint32_t _frame, ae_bool_t _interpolate, ae_float_t t )
{
    if( _property->immutable_r == AE_TRUE )
    {
        return _property->immutable_color_r;
    }

    if( _interpolate == AE_FALSE )
    {
        ae_color_t c = _property->colors_r[_frame];

        ae_float_t cf = ae_tof_c( c );

        return cf;
    }

    ae_color_t c0 = _property->colors_r[_frame + 0];
    ae_color_t c1 = _property->colors_r[_frame + 1];

    ae_float_t cf = ae_linerp_c( c0, c1, t );

    return cf;
}
//////////////////////////////////////////////////////////////////////////
static ae_float_t __compute_movie_property_color_g( const struct aeMoviePropertyColor * _property, ae_uint32_t _frame, ae_bool_t _interpolate, ae_float_t t )
{
    if( _property->immutable_g == AE_TRUE )
    {
        return _property->immutable_color_g;
    }

    if( _interpolate == AE_FALSE )
    {
        ae_color_t c = _property->colors_g[_frame];

        ae_float_t cf = ae_tof_c( c );

        return cf;
    }

    ae_color_t c0 = _property->colors_g[_frame + 0];
    ae_color_t c1 = _property->colors_g[_frame + 1];

    ae_float_t cf = ae_linerp_c( c0, c1, t );

    return cf;
}
//////////////////////////////////////////////////////////////////////////
static ae_float_t __compute_movie_property_color_b( const struct aeMoviePropertyColor * _property, ae_uint32_t _frame, ae_bool_t _interpolate, ae_float_t t )
{
    if( _property->immutable_b == AE_TRUE )
    {
        return _property->immutable_color_b;
    }

    if( _interpolate == AE_FALSE )
    {
        ae_color_t c = _property->colors_b[_frame];

        ae_float_t cf = ae_tof_c( c );

        return cf;
    }

    ae_color_t c0 = _property->colors_b[_frame + 0];
    ae_color_t c1 = _property->colors_b[_frame + 1];

    ae_float_t cf = ae_linerp_c( c0, c1, t );

    return cf;
}
//////////////////////////////////////////////////////////////////////////
#	ifdef AE_MOVIE_DEBUG
static ae_bool_t __test_error_composition_layer_frame( const aeMovieInstance * _instance, const aeMovieCompositionData * _compositionData, const aeMovieLayerData * _layerData, ae_uint32_t _frameId, const ae_char_t * _msg )
{
    if( _frameId >= _layerData->frame_count )
    {
        _instance->logger( _instance->instance_data
            , AE_ERROR_INTERNAL
            , "composition '%s' layer '%s' - %s\n"
            , _compositionData->name
            , _layerData->name
            , _msg
        );

        return AE_FALSE;
    }

    return AE_TRUE;
}
#	endif
//////////////////////////////////////////////////////////////////////////
static ae_uint32_t __get_movie_frame_time( const struct aeMovieCompositionAnimation * _animation, const struct aeMovieNode * _node, ae_bool_t _interpolate, ae_float_t * _t )
{
    ae_float_t animation_time = _animation->time;

    const aeMovieLayerData * layer = _node->layer;

    ae_float_t frameDurationInv = layer->composition_data->frameDurationInv;

    ae_float_t current_time = animation_time - _node->in_time + _node->start_time;

    ae_float_t frame_time = current_time / _node->stretch * frameDurationInv;

    if( frame_time < 0.f )
    {
        frame_time = 0.f;
    }

    ae_uint32_t frame_relative = (ae_uint32_t)frame_time;

    if( _interpolate == AE_TRUE )
    {
        ae_float_t t_relative = frame_time - (ae_float_t)frame_relative;

        if( frame_relative >= layer->frame_count )
        {
            frame_relative = layer->frame_count - 1;

            t_relative = 0.f;
        }

        *_t = t_relative;
    }
    else
    {
        if( frame_relative >= layer->frame_count )
        {
            frame_relative = layer->frame_count - 1;
        }
    }

    return frame_relative;
}
//////////////////////////////////////////////////////////////////////////
static void __update_movie_composition_node_matrix( const aeMovieComposition * _composition, const aeMovieCompositionData * _compositionData, const aeMovieCompositionAnimation * _animation, aeMovieNode * _node, ae_uint32_t _revision, ae_uint32_t _frameId, ae_bool_t _interpolate, ae_float_t _t )
{
    if( _node->update_revision == _revision )
    {
        return;
    }

    const aeMovieLayerData * layer = _node->layer;

#	ifdef AE_MOVIE_DEBUG	
    if( __test_error_composition_layer_frame( _composition->movie_data->instance
        , _compositionData
        , layer
        , _frameId
        , "__update_movie_composition_node_matrix frame id out count"
    ) == AE_FALSE )
    {
        return;
    }
#	endif

    ae_float_t local_r = 1.f;
    ae_float_t local_g = 1.f;
    ae_float_t local_b = 1.f;

    if( layer->color_vertex != AE_NULL )
    {
        const struct aeMoviePropertyColor * property_color = layer->color_vertex->property_color;

        local_r = __compute_movie_property_color_r( property_color, _frameId, _interpolate, _t );
        local_g = __compute_movie_property_color_g( property_color, _frameId, _interpolate, _t );
        local_b = __compute_movie_property_color_g( property_color, _frameId, _interpolate, _t );
    }

    if( _node->relative_node == AE_NULL )
    {
        ae_float_t local_opacity = ae_movie_make_layer_opacity( layer->transformation, _frameId, _interpolate, _t );

        if( _interpolate == AE_TRUE )
        {
            ae_movie_make_layer_transformation_interpolate( _node->matrix, layer->transformation, _frameId, _t );
        }
        else
        {
            ae_movie_make_layer_transformation_fixed( _node->matrix, layer->transformation, _frameId );
        }

        if( layer->sub_composition_data != AE_NULL )
        {
            _node->composition_opactity = local_opacity;

            _node->composition_r = local_r;
            _node->composition_g = local_g;
            _node->composition_b = local_b;
        }
        else
        {
            _node->composition_opactity = 1.f;

            _node->composition_r = 1.f;
            _node->composition_g = 1.f;
            _node->composition_b = 1.f;
        }

        _node->opacity = local_opacity;

        _node->r = local_r;
        _node->g = local_g;
        _node->b = local_b;

        return;
    }

    aeMovieNode * node_relative = _node->relative_node;

    if( node_relative->update_revision != _revision )
    {
        ae_float_t t_relative = 0.f;
        ae_uint32_t frame_relative = __get_movie_frame_time( _animation, node_relative, _composition->interpolate, &t_relative );

        __update_movie_composition_node_matrix( _composition, _compositionData, _animation, node_relative, _revision, frame_relative, _interpolate, t_relative );
    }

    ae_float_t local_opacity = ae_movie_make_layer_opacity( layer->transformation, _frameId, _interpolate, _t );

    ae_matrix4_t local_matrix;

    if( _interpolate == AE_TRUE )
    {
        ae_movie_make_layer_transformation_interpolate( local_matrix, layer->transformation, _frameId, _t );
    }
    else
    {
        ae_movie_make_layer_transformation_fixed( local_matrix, layer->transformation, _frameId );
    }

    ae_mul_m4_m4_r( _node->matrix, local_matrix, node_relative->matrix );

    if( layer->sub_composition_data != AE_NULL )
    {
        _node->composition_opactity = node_relative->composition_opactity * local_opacity;

        _node->composition_r = node_relative->composition_r * local_r;
        _node->composition_g = node_relative->composition_g * local_g;
        _node->composition_b = node_relative->composition_b * local_b;
    }
    else
    {
        _node->composition_opactity = node_relative->composition_opactity;

        _node->composition_r = node_relative->composition_r;
        _node->composition_g = node_relative->composition_g;
        _node->composition_b = node_relative->composition_b;
    }

    _node->opacity = node_relative->composition_opactity * local_opacity;

    _node->r = node_relative->composition_r * local_r;
    _node->g = node_relative->composition_g * local_g;
    _node->b = node_relative->composition_b * local_b;
}
//////////////////////////////////////////////////////////////////////////
static void __update_movie_composition_node_shader( const aeMovieComposition * _composition, const aeMovieCompositionData * _compositionData, aeMovieNode * _node, ae_uint32_t _revision, ae_uint32_t _frameId, ae_bool_t _interpolate, ae_float_t _t )
{
    if( _node->update_revision == _revision )
    {
        return;
    }

    const aeMovieLayerData * layer = _node->layer;

#	ifdef AE_MOVIE_DEBUG	
    if( __test_error_composition_layer_frame( _composition->movie_data->instance
        , _compositionData
        , layer
        , _frameId
        , "__update_movie_composition_node_shader frame id out count"
    ) == AE_FALSE )
    {
        return;
    }
#	endif

    const aeMovieLayerShader * shader = layer->shader;
    
    const struct aeMovieLayerShaderParameter ** it_parameter = shader->parameters;
    const struct aeMovieLayerShaderParameter ** it_parameter_end = shader->parameters + shader->parameter_count;

    ae_uint8_t index = 0;

    for( ;
        it_parameter != it_parameter_end;
        ++it_parameter )
    {
        const struct aeMovieLayerShaderParameter * parameter = *it_parameter;

        switch( parameter->type )
        {
        case 3:
            {
                const struct aeMovieLayerShaderParameterSlider * parameter_slider = (const struct aeMovieLayerShaderParameterSlider *)parameter;
                
                float value = __compute_movie_property_value( parameter_slider->property_value, _frameId, _interpolate, _t );

                aeMovieShaderPropertyUpdateCallbackData callbackData;
                callbackData.element = _node->shader_data;
                callbackData.index = index;
                callbackData.name = parameter_slider->name;
                callbackData.uniform = parameter_slider->uniform;
                callbackData.type = parameter_slider->type;
                callbackData.color_r = 0.f;
                callbackData.color_g = 0.f;
                callbackData.color_b = 0.f;
                callbackData.value = value;

                (*_composition->providers.shader_property_update)(&callbackData, _composition->provider_data);
            }break;
        case 5:
            {                                                                                                                                   
                const struct aeMovieLayerShaderParameterColor * parameter_color = (const struct aeMovieLayerShaderParameterColor *)parameter;

                ae_float_t color_r = __compute_movie_property_color_r( parameter_color->property_color, _frameId, _interpolate, _t );
                ae_float_t color_g = __compute_movie_property_color_g( parameter_color->property_color, _frameId, _interpolate, _t );
                ae_float_t color_b = __compute_movie_property_color_b( parameter_color->property_color, _frameId, _interpolate, _t );

                aeMovieShaderPropertyUpdateCallbackData callbackData;
                callbackData.element = _node->shader_data;
                callbackData.index = index;
                callbackData.name = parameter_color->name;
                callbackData.uniform = parameter_color->uniform;
                callbackData.type = parameter_color->type;
                callbackData.color_r = color_r;
                callbackData.color_g = color_g;
                callbackData.color_b = color_b;
                callbackData.value = 0.f;

                (*_composition->providers.shader_property_update)(&callbackData, _composition->provider_data);
            }break;
        }

        ++index;
    }
}
//////////////////////////////////////////////////////////////////////////
static ae_uint32_t __get_movie_composition_data_node_count( const aeMovieCompositionData * _compositionData )
{
    ae_uint32_t count = _compositionData->layer_count;

    const aeMovieLayerData * it_layer = _compositionData->layers;
    const aeMovieLayerData * it_layer_end = _compositionData->layers + _compositionData->layer_count;
    for( ; it_layer != it_layer_end; ++it_layer )
    {
        const aeMovieLayerData * layer = it_layer;

        aeMovieLayerTypeEnum layer_type = layer->type;

        switch( layer_type )
        {
        case AE_MOVIE_LAYER_TYPE_MOVIE:
        case AE_MOVIE_LAYER_TYPE_SUB_MOVIE:
            {
                ae_uint32_t movie_layer_count = __get_movie_composition_data_node_count( layer->sub_composition_data );

                count += movie_layer_count;
            }break;
        default:
            {
            }break;
        }
    }

    return count;
}
//////////////////////////////////////////////////////////////////////////
static ae_bool_t __setup_movie_node_track_matte( aeMovieNode * _nodes, ae_uint32_t * _iterator, const aeMovieCompositionData * _compositionData, aeMovieNode * _trackMatte )
{
    const aeMovieLayerData * it_layer = _compositionData->layers;
    const aeMovieLayerData * it_layer_end = _compositionData->layers + _compositionData->layer_count;
    for( ; it_layer != it_layer_end; ++it_layer )
    {
        const aeMovieLayerData * layer = it_layer;

        aeMovieNode * node = _nodes + ((*_iterator)++);

        aeMovieLayerTypeEnum layer_type = node->layer->type;

        if( _trackMatte == AE_NULL )
        {
            if( layer->has_track_matte == AE_TRUE )
            {
                switch( layer_type )
                {
                case AE_MOVIE_LAYER_TYPE_MOVIE:
                case AE_MOVIE_LAYER_TYPE_SUB_MOVIE:
                    {
                        ae_uint32_t sub_composition_node_count = __get_movie_composition_data_node_count( node->layer->sub_composition_data );

                        aeMovieNode * track_matte_node = _nodes + (*_iterator) + sub_composition_node_count;

                        node->track_matte_node = track_matte_node;
                        node->track_matte_data = AE_NULL;
                    }break;
                default:
                    {
                        aeMovieNode * track_matte_node = _nodes + (*_iterator);

                        node->track_matte_node = track_matte_node;
                        node->track_matte_data = AE_NULL;
                    }break;
                }
            }
            else
            {
                node->track_matte_node = AE_NULL;
                node->track_matte_data = AE_NULL;
            }
        }
        else
        {
            if( layer->has_track_matte == AE_TRUE )
            {
                return AE_FALSE;
            }
            else
            {
                node->track_matte_node = _trackMatte;
                node->track_matte_data = AE_NULL;
            }
        }

        switch( layer_type )
        {
        case AE_MOVIE_LAYER_TYPE_MOVIE:
        case AE_MOVIE_LAYER_TYPE_SUB_MOVIE:
            {
                if( __setup_movie_node_track_matte( _nodes, _iterator, layer->sub_composition_data, node->track_matte_node ) == AE_FALSE )
                {
                    return AE_FALSE;
                }
            }break;
        default:
            {
            }break;
        }
    }

    return AE_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static ae_bool_t __setup_movie_node_track_matte2( aeMovieComposition * _composition )
{
    aeMovieNode * it_node = _composition->nodes;
    aeMovieNode * it_node_end = _composition->nodes + _composition->node_count;
    for( ; it_node != it_node_end; ++it_node )
    {
        aeMovieNode * node = it_node;

        if( node->ignore == AE_TRUE )
        {
            continue;
        }

        const aeMovieLayerData * layer = node->layer;

        if( layer->is_track_matte == AE_TRUE )
        {
            aeMovieRenderMesh mesh;
            __compute_movie_node( _composition, node, &mesh, _composition->interpolate, AE_TRUE );

            aeMovieTrackMatteProviderCallbackData callbackData;
            callbackData.node_element = node->element_data;
            callbackData.type = layer->type;
            callbackData.loop = AE_FALSE;
            callbackData.offset = node->start_time;
            callbackData.matrix = node->matrix;
            callbackData.opacity = node->opacity;
            callbackData.mesh = &mesh;

            ae_voidptr_t track_matte_data = (*_composition->providers.track_matte_provider)(&callbackData, _composition->provider_data);
            node->track_matte_data = track_matte_data;
        }
    }

    return AE_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static ae_uint32_t __get_movie_subcomposition_count( aeMovieComposition * _composition )
{
    ae_uint32_t count = 0;

    aeMovieNode * it_node = _composition->nodes;
    aeMovieNode * it_node_end = _composition->nodes + _composition->node_count;
    for( ; it_node != it_node_end; ++it_node )
    {
        aeMovieNode * node = it_node;

        if( node->ignore == AE_TRUE )
        {
            continue;
        }

        const aeMovieLayerData * layer = node->layer;

        if( layer->type != AE_MOVIE_LAYER_TYPE_SUB_MOVIE )
        {
            continue;
        }

        ++count;
    }

    return count;
}
//////////////////////////////////////////////////////////////////////////
static ae_bool_t __setup_movie_subcomposition2( aeMovieComposition * _composition, ae_uint32_t * _node_iterator, aeMovieSubComposition * _subcompositions, ae_uint32_t * _subcomposition_iterator, const aeMovieCompositionData * _compositionData, const aeMovieSubComposition * _subcomposition )
{
    const aeMovieLayerData * it_layer = _compositionData->layers;
    const aeMovieLayerData * it_layer_end = _compositionData->layers + _compositionData->layer_count;
    for( ; it_layer != it_layer_end; ++it_layer )
    {
        const aeMovieLayerData * layer = it_layer;

        aeMovieNode * node = _composition->nodes + ((*_node_iterator)++);

        const aeMovieLayerData * node_layer = node->layer;

        aeMovieLayerTypeEnum layer_type = node_layer->type;

        node->subcomposition = _subcomposition;

        switch( layer_type )
        {
        case AE_MOVIE_LAYER_TYPE_MOVIE:
            {
                if( __setup_movie_subcomposition2( _composition, _node_iterator, _subcompositions, _subcomposition_iterator, layer->sub_composition_data, _subcomposition ) == AE_FALSE )
                {
                    return AE_FALSE;
                }
            }break;
        case AE_MOVIE_LAYER_TYPE_SUB_MOVIE:
            {
                aeMovieSubComposition * subcomposition = _subcompositions + ((*_subcomposition_iterator)++);

                subcomposition->layer = layer;
                subcomposition->composition_data = layer->composition_data;

                aeMovieCompositionAnimation * animation = AE_NEW( _composition->movie_data->instance, aeMovieCompositionAnimation );

                animation->play = AE_FALSE;
                animation->pause = AE_FALSE;
                animation->interrupt = AE_FALSE;

                animation->loop = AE_FALSE;

                animation->time = 0.f;

                const aeMovieCompositionData * sub_composition_data = layer->sub_composition_data;

                animation->loop_segment_begin = sub_composition_data->loop_segment[0];
                animation->loop_segment_end = sub_composition_data->loop_segment[1];

                animation->work_area_begin = 0.f;
                animation->work_area_end = sub_composition_data->duration;

                subcomposition->animation = animation;

                if( __setup_movie_subcomposition2( _composition, _node_iterator, _subcompositions, _subcomposition_iterator, layer->sub_composition_data, subcomposition ) == AE_FALSE )
                {
                    return AE_FALSE;
                }
            }break;
        default:
            {
            }break;
        }
    }

    return AE_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static ae_bool_t __setup_movie_subcomposition( aeMovieComposition * _composition )
{
    ae_uint32_t subcomposition_count = __get_movie_subcomposition_count( _composition );

    aeMovieSubComposition * subcompositions = AE_NEWN( _composition->movie_data->instance, aeMovieSubComposition, subcomposition_count );

    ae_uint32_t node_iterator = 0;
    ae_uint32_t subcomposition_iterator = 0;

    if( __setup_movie_subcomposition2( _composition, &node_iterator, subcompositions, &subcomposition_iterator, _composition->composition_data, AE_NULL ) == AE_FALSE )
    {
        return AE_FALSE;
    }

    _composition->subcomposition_count = subcomposition_count;
    _composition->subcompositions = subcompositions;

    return AE_TRUE;
}
//////////////////////////////////////////////////////////////////////////
static void __setup_movie_node_relative( aeMovieNode * _nodes, ae_uint32_t * _iterator, const aeMovieCompositionData * _compositionData, aeMovieNode * _parent )
{
    ae_uint32_t begin_index = *_iterator;

    const aeMovieLayerData *it_layer = _compositionData->layers;
    const aeMovieLayerData *it_layer_end = _compositionData->layers + _compositionData->layer_count;
    for( ; it_layer != it_layer_end; ++it_layer )
    {
        const aeMovieLayerData * layer = it_layer;

        aeMovieNode * node = _nodes + ((*_iterator)++);

        node->layer = layer;
        node->update_revision = 0;

        node->active = AE_FALSE;
        node->ignore = AE_FALSE;
        node->enable = AE_TRUE;
        node->animate = AE_MOVIE_NODE_ANIMATE_STATIC;

        if( layer->parent_index == 0 )
        {
            node->relative_node = _parent;
        }

        node->current_time = 0.f;

        switch( layer->type )
        {
        case AE_MOVIE_LAYER_TYPE_MOVIE:
        case AE_MOVIE_LAYER_TYPE_SUB_MOVIE:
            {
                __setup_movie_node_relative( _nodes, _iterator, layer->sub_composition_data, node );
            }break;
        default:
            {
            }break;
        }
    }

    ae_uint32_t end_index = *_iterator;

    const aeMovieLayerData *it_layer2 = _compositionData->layers;
    const aeMovieLayerData *it_layer2_end = _compositionData->layers + _compositionData->layer_count;
    for( ; it_layer2 != it_layer2_end; ++it_layer2 )
    {
        const aeMovieLayerData * layer = it_layer2;

        ae_uint32_t parent_index = layer->parent_index;

        if( parent_index == 0 )
        {
            continue;
        }

        aeMovieNode * node = __find_node_by_layer( _nodes, begin_index, end_index, layer );

        const aeMovieLayerData * parent_layer = __find_layer_by_index( _compositionData, parent_index );

        aeMovieNode * parent_node = __find_node_by_layer( _nodes, begin_index, end_index, parent_layer );

        node->relative_node = parent_node;
    }
}
//////////////////////////////////////////////////////////////////////////
static void __setup_movie_node_time( aeMovieNode * _nodes, ae_uint32_t * _iterator, const aeMovieCompositionData * _compositionData, const aeMovieNode * _parent, ae_float_t _stretch, ae_float_t _startTime )
{
    const aeMovieLayerData *it_layer = _compositionData->layers;
    const aeMovieLayerData *it_layer_end = _compositionData->layers + _compositionData->layer_count;
    for( ; it_layer != it_layer_end; ++it_layer )
    {
        const aeMovieLayerData * layer = it_layer;

        aeMovieNode * node = _nodes + ((*_iterator)++);

        if( _parent == AE_NULL )
        {
            node->start_time = 0.f;
            node->in_time = layer->in_time;
            node->out_time = layer->out_time;
        }
        else
        {
            //node->start_time = _startTime;

            ae_float_t layer_in = layer->in_time * _stretch - _startTime;
            ae_float_t parent_in = _parent->in_time;

            if( parent_in > layer_in )
            {
                node->start_time = parent_in - layer_in;
                node->in_time = parent_in;
            }
            else
            {
                node->start_time = 0.f;
                node->in_time = layer_in;
            }

            //node->in_time = max_f_f( layer_in, parent_in );

            ae_float_t layer_out = layer->out_time * _stretch - _startTime;
            ae_float_t parent_out = _parent->out_time;

            if( node->subcomposition == _parent->subcomposition )
            {
                node->out_time = ae_min_f_f( layer_out, parent_out );
            }
            else
            {
                node->out_time = layer->out_time;
            }

            if( node->out_time <= 0.f || node->out_time < node->in_time )
            {
                node->in_time = 0.f;
                node->out_time = 0.f;
                node->ignore = AE_TRUE;
            }
        }

        node->stretch = _stretch;

        switch( layer->type )
        {
        case AE_MOVIE_LAYER_TYPE_MOVIE:
        case AE_MOVIE_LAYER_TYPE_SUB_MOVIE:
            {
                ae_float_t to_stretch = _stretch * layer->stretch;
                ae_float_t to_startTime = _startTime + layer->start_time - layer->in_time;

                __setup_movie_node_time( _nodes, _iterator, layer->sub_composition_data, node, to_stretch, to_startTime );
            }break;
        default:
            {
            }break;
        }
    }
}
//////////////////////////////////////////////////////////////////////////
static void __setup_movie_node_blend_mode( aeMovieNode * _nodes, ae_uint32_t * _iterator, const aeMovieCompositionData * _compositionData, const aeMovieNode * _parent, aeMovieBlendMode _blendMode )
{
    (void)_parent; //TODO

    const aeMovieLayerData *it_layer = _compositionData->layers;
    const aeMovieLayerData *it_layer_end = _compositionData->layers + _compositionData->layer_count;
    for( ; it_layer != it_layer_end; ++it_layer )
    {
        const aeMovieLayerData * layer = it_layer;

        aeMovieNode * node = _nodes + ((*_iterator)++);

        if( _blendMode != AE_MOVIE_BLEND_NORMAL )
        {
            node->blend_mode = _blendMode;
        }
        else
        {
            node->blend_mode = layer->blend_mode;
        }

        aeMovieBlendMode composition_blend_mode = AE_MOVIE_BLEND_NORMAL;

        if( layer->sub_composition_data != AE_NULL )
        {
            composition_blend_mode = node->blend_mode;
        }

        switch( layer->type )
        {
        case AE_MOVIE_LAYER_TYPE_MOVIE:
        case AE_MOVIE_LAYER_TYPE_SUB_MOVIE:
            {
                __setup_movie_node_blend_mode( _nodes, _iterator, layer->sub_composition_data, node, composition_blend_mode );
            }break;
        default:
            {
            }break;
        }
    }
}
//////////////////////////////////////////////////////////////////////////
static void __setup_movie_node_camera2( aeMovieComposition * _composition, uint32_t * _iterator, const aeMovieCompositionData * _compositionData, void * _cameraData )
{
    const aeMovieLayerData *it_layer = _compositionData->layers;
    const aeMovieLayerData *it_layer_end = _compositionData->layers + _compositionData->layer_count;
    for( ; it_layer != it_layer_end; ++it_layer )
    {
        const aeMovieLayerData * layer = it_layer;

        aeMovieNode * node = _composition->nodes + ((*_iterator)++);

        if( layer->threeD == AE_TRUE )
        {
            node->camera_data = _composition->camera_data;
        }
        else
        {
            node->camera_data = _cameraData;
        }

        switch( layer->type )
        {
        case AE_MOVIE_LAYER_TYPE_SUB_MOVIE:
        case AE_MOVIE_LAYER_TYPE_MOVIE:
            {
                __setup_movie_node_camera2( _composition, _iterator, layer->sub_composition_data, node->camera_data );
            }break;
        default:
            {
            }break;
        }
    }
}
//////////////////////////////////////////////////////////////////////////
static void __setup_movie_node_camera( aeMovieComposition * _composition )
{
    const aeMovieCompositionData * composition_data = _composition->composition_data;

    if( composition_data->camera == AE_NULL )
    {
        _composition->camera_data = AE_NULL;

        aeMovieNode *it_node = _composition->nodes;
        aeMovieNode *it_node_end = _composition->nodes + _composition->node_count;
        for( ; it_node != it_node_end; ++it_node )
        {
            aeMovieNode * node = it_node;

            node->camera_data = AE_NULL;
        }

        return;
    }

    const aeMovieCompositionCamera * camera = composition_data->camera;

    ae_float_t width = composition_data->width;
    ae_float_t height = composition_data->height;

    aeMovieCameraProviderCallbackData callbackData;
    callbackData.name = composition_data->name;
    callbackData.fov = camera->fov;
    callbackData.width = width;
    callbackData.height = height;

    ae_movie_make_camera_transformation( callbackData.target, callbackData.position, callbackData.quaternion, camera, 0, AE_FALSE, 0.f );

    _composition->camera_data = (*_composition->providers.camera_provider)(&callbackData, _composition->provider_data);

    uint32_t node_camera_iterator = 0;
    __setup_movie_node_camera2( _composition, &node_camera_iterator, composition_data, AE_NULL );
}
//////////////////////////////////////////////////////////////////////////
static void __setup_movie_node_matrix2( const aeMovieComposition * _composition, const aeMovieCompositionData * _compositionData, const aeMovieCompositionAnimation * _animation, const aeMovieSubComposition * _subcomposition )
{
    ae_uint32_t update_revision = __get_composition_update_revision( _composition );

    aeMovieNode *it_node = _composition->nodes;
    aeMovieNode *it_node_end = _composition->nodes + _composition->node_count;
    for( ; it_node != it_node_end; ++it_node )
    {
        aeMovieNode * node = it_node;

        if( node->subcomposition != _subcomposition )
        {
            continue;
        }

        ae_float_t t = 0.f;
        ae_uint32_t frameId = __get_movie_frame_time( _animation, node, _composition->interpolate, &t );

        __update_movie_composition_node_matrix( _composition, _compositionData, _animation, node, update_revision, frameId, _composition->interpolate, t );
    }
}
//////////////////////////////////////////////////////////////////////////
static void __setup_movie_node_matrix( const aeMovieComposition * _composition )
{
    const aeMovieCompositionData * composition_data = _composition->composition_data;
    const aeMovieCompositionAnimation * animation = _composition->animation;

    __setup_movie_node_matrix2( _composition, composition_data, animation, AE_NULL );

    const aeMovieSubComposition *it_subcomposition = _composition->subcompositions;
    const aeMovieSubComposition *it_subcomposition_end = _composition->subcompositions + _composition->subcomposition_count;

    for( ; it_subcomposition != it_subcomposition_end; ++it_subcomposition )
    {
        const aeMovieSubComposition * subcomposition = it_subcomposition;

        const aeMovieCompositionData * subcomposition_composition_data = subcomposition->composition_data;
        aeMovieCompositionAnimation * subcomposition_animation = subcomposition->animation;

        __setup_movie_node_matrix2( _composition, subcomposition_composition_data, subcomposition_animation, subcomposition );
    }
}
//////////////////////////////////////////////////////////////////////////
static void __setup_movie_node_shader( aeMovieComposition * _composition )
{
    aeMovieNode* it_node = _composition->nodes;
    aeMovieNode* it_node_end = _composition->nodes + _composition->node_count;
    for( ; it_node != it_node_end; ++it_node )
    {
        aeMovieNode * node = it_node;

        const aeMovieLayerData * layer = node->layer;

        if( layer->shader == AE_NULL )
        {
            node->shader_data = AE_NULL;

            continue;
        }

        const aeMovieLayerShader * shader = layer->shader;

        aeMovieShaderProviderCallbackData callbackData;
        callbackData.name = shader->name;
        callbackData.version = shader->version;
        callbackData.shader_vertex = shader->shader_vertex;
        callbackData.shader_fragment = shader->shader_fragment;
        callbackData.parameter_count = shader->parameter_count;
        
        const struct aeMovieLayerShaderParameter ** it_parameter = shader->parameters;
        const struct aeMovieLayerShaderParameter ** it_parameter_end = shader->parameters + shader->parameter_count;

        uint32_t paremeter_index = 0;

        for( ;
            it_parameter != it_parameter_end;
            ++it_parameter )
        {
            const struct aeMovieLayerShaderParameter * parameter = *it_parameter;

            callbackData.parameter_names[paremeter_index] = parameter->name;
            callbackData.parameter_uniforms[paremeter_index] = parameter->uniform;
            callbackData.parameter_types[paremeter_index] = parameter->type;
            paremeter_index++;
        }
        
        ae_voidptr_t shader_data = (*_composition->providers.shader_provider)(&callbackData, _composition->provider_data);

        node->shader_data = shader_data;
    }
}
//////////////////////////////////////////////////////////////////////////
static void __setup_movie_composition_element( aeMovieComposition * _composition )
{
    aeMovieNode* it_node = _composition->nodes;
    aeMovieNode* it_node_end = _composition->nodes + _composition->node_count;
    for( ; it_node != it_node_end; ++it_node )
    {
        aeMovieNode * node = it_node;

        const aeMovieLayerData * track_matte_layer = node->track_matte_node == AE_NULL ? AE_NULL : node->track_matte_node->layer;

        aeMovieNodeProviderCallbackData callbackData;
        callbackData.layer = node->layer;
        callbackData.matrix = node->matrix;
        callbackData.opacity = node->opacity;
        callbackData.track_matte_layer = track_matte_layer;

        ae_voidptr_t element_data = (*_composition->providers.node_provider)(&callbackData, _composition->provider_data);

        node->element_data = element_data;
    }
}
//////////////////////////////////////////////////////////////////////////
static void __setup_movie_composition_active( aeMovieComposition * _composition )
{
    aeMovieNode *it_node = _composition->nodes;
    aeMovieNode *it_node_end = _composition->nodes + _composition->node_count;
    for( ; it_node != it_node_end; ++it_node )
    {
        aeMovieNode * node = it_node;

        if( node->ignore == AE_TRUE )
        {
            continue;
        }

        if( ae_equal_f_z( node->in_time ) == AE_TRUE )
        {
            node->active = AE_TRUE;
        }
    }
}
//////////////////////////////////////////////////////////////////////////
static ae_voidptr_t __dummy_ae_movie_camera_provider( const aeMovieCameraProviderCallbackData * _callbackData, ae_voidptr_t _data )
{
    (void)_callbackData;
    (void)_data;

    return AE_NULL;
}
//////////////////////////////////////////////////////////////////////////
static void __dummy_ae_movie_camera_destroy( const aeMovieCameraDeleterCallbackData * _callbackData, ae_voidptr_t _data )
{
    (void)_callbackData;
    (void)_data;
}
//////////////////////////////////////////////////////////////////////////
static void __dummy_ae_movie_camera_update( const aeMovieCameraUpdateCallbackData * _callbackData, ae_voidptr_t _data )
{
    (void)_callbackData;
    (void)_data;
}
//////////////////////////////////////////////////////////////////////////
static ae_voidptr_t __dummy_ae_movie_node_provider( const aeMovieNodeProviderCallbackData * _callbackData, ae_voidptr_t _data )
{
    (void)_callbackData;
    (void)_data;

    return AE_NULL;
}
//////////////////////////////////////////////////////////////////////////
static void __dummy_ae_movie_node_destroyer( const aeMovieNodeDeleterCallbackData * _callbackData, ae_voidptr_t _data )
{
    (void)_callbackData;
    (void)_data;
}
//////////////////////////////////////////////////////////////////////////
static void __dummy_ae_movie_node_update( const aeMovieNodeUpdateCallbackData * _callbackData, ae_voidptr_t _data )
{
    (void)_callbackData;
    (void)_data;
}
//////////////////////////////////////////////////////////////////////////
static ae_voidptr_t __dummy_ae_movie_track_matte_provider( const aeMovieTrackMatteProviderCallbackData * _callbackData, ae_voidptr_t _data )
{
    (void)_callbackData;
    (void)_data;

    return AE_NULL;
}
//////////////////////////////////////////////////////////////////////////
static void __dummy_ae_movie_track_matte_update( const aeMovieTrackMatteUpdateCallbackData * _callbackData, ae_voidptr_t _data )
{
    (void)_callbackData;
    (void)_data;

}
//////////////////////////////////////////////////////////////////////////
static void __dummy_ae_movie_track_matte_deleter( const aeMovieTrackMatteDeleterCallbackData * _callbackData, ae_voidptr_t _data )
{
    (void)_callbackData;
    (void)_data;

}
//////////////////////////////////////////////////////////////////////////
static ae_voidptr_t __dummy_ae_movie_shader_provider( const aeMovieShaderProviderCallbackData * _callbackData, ae_voidptr_t _data )
{
    (void)_callbackData;
    (void)_data;

    return AE_NULL;
}
//////////////////////////////////////////////////////////////////////////
static void __dummy_ae_movie_shader_deleter( const aeMovieShaderDeleterCallbackData * _callbackData, ae_voidptr_t _data )
{
    (void)_callbackData;
    (void)_data;
}
//////////////////////////////////////////////////////////////////////////
static void __dummy_ae_movie_shader_property_update( const aeMovieShaderPropertyUpdateCallbackData * _callbackData, ae_voidptr_t _data )
{
    (void)_callbackData;
    (void)_data;
}
//////////////////////////////////////////////////////////////////////////
static void __dummy_ae_movie_composition_event( const aeMovieCompositionEventCallbackData * _callbackData, ae_voidptr_t _data )
{
    (void)_callbackData;
    (void)_data;
}
//////////////////////////////////////////////////////////////////////////
static void __dummy_ae_movie_composition_state( const aeMovieCompositionStateCallbackData * _callbackData, ae_voidptr_t _data )
{
    (void)_callbackData;
    (void)_data;
}
//////////////////////////////////////////////////////////////////////////
void ae_initialize_movie_composition_providers( aeMovieCompositionProviders * _providers )
{
    _providers->camera_provider = &__dummy_ae_movie_camera_provider;
    _providers->camera_deleter = &__dummy_ae_movie_camera_destroy;
    _providers->camera_update = &__dummy_ae_movie_camera_update;
    _providers->node_provider = &__dummy_ae_movie_node_provider;
    _providers->node_deleter = &__dummy_ae_movie_node_destroyer;
    _providers->node_update = &__dummy_ae_movie_node_update;
    _providers->track_matte_provider = &__dummy_ae_movie_track_matte_provider;
    _providers->track_matte_update = &__dummy_ae_movie_track_matte_update;
    _providers->track_matte_deleter = &__dummy_ae_movie_track_matte_deleter;
    _providers->shader_provider = &__dummy_ae_movie_shader_provider;
    _providers->shader_deleter = &__dummy_ae_movie_shader_deleter;
    _providers->shader_property_update = &__dummy_ae_movie_shader_property_update;
    _providers->composition_event = &__dummy_ae_movie_composition_event;
    _providers->composition_state = &__dummy_ae_movie_composition_state;
}
//////////////////////////////////////////////////////////////////////////
aeMovieComposition * ae_create_movie_composition( const aeMovieData * _movieData, const aeMovieCompositionData * _compositionData, ae_bool_t _interpolate, const aeMovieCompositionProviders * providers, ae_voidptr_t _data )
{
    aeMovieComposition * composition = AE_NEW( _movieData->instance, aeMovieComposition );

    composition->movie_data = _movieData;
    composition->composition_data = _compositionData;

    aeMovieCompositionAnimation * animation = AE_NEW( _movieData->instance, aeMovieCompositionAnimation );

    animation->play = AE_FALSE;
    animation->pause = AE_FALSE;
    animation->interrupt = AE_FALSE;
    animation->loop = AE_FALSE;

    animation->time = 0.f;
    animation->loop_segment_begin = _compositionData->loop_segment[0];
    animation->loop_segment_end = _compositionData->loop_segment[1];
    animation->work_area_begin = 0.f;
    animation->work_area_end = _compositionData->duration;

    composition->animation = animation;

    composition->update_revision = AE_NEW( _movieData->instance, ae_uint32_t );
    *composition->update_revision = 0;

    composition->interpolate = _interpolate;

    ae_uint32_t node_count = __get_movie_composition_data_node_count( _compositionData );

    composition->node_count = node_count;
    composition->nodes = AE_NEWN( _movieData->instance, aeMovieNode, node_count );

    composition->providers = *providers;
    composition->provider_data = _data;

    ae_uint32_t node_relative_iterator = 0;
    __setup_movie_node_relative( composition->nodes, &node_relative_iterator, _compositionData, AE_NULL );

    if( __setup_movie_subcomposition( composition ) == AE_FALSE )
    {
        return AE_NULL;
    }

    ae_uint32_t node_time_iterator = 0;
    __setup_movie_node_time( composition->nodes, &node_time_iterator, _compositionData, AE_NULL, 1.f, 0.f );

    ae_uint32_t node_blend_mode_iterator = 0;
    __setup_movie_node_blend_mode( composition->nodes, &node_blend_mode_iterator, _compositionData, AE_NULL, AE_MOVIE_BLEND_NORMAL );

    __inc_composition_update_revision( composition );

    __setup_movie_node_camera( composition );

    __setup_movie_node_matrix( composition );

    __setup_movie_composition_active( composition );

    ae_uint32_t node_track_matte_iterator = 0;
    if( __setup_movie_node_track_matte( composition->nodes, &node_track_matte_iterator, _compositionData, AE_NULL ) == AE_FALSE )
    {
        return AE_NULL;
    }

    __setup_movie_node_shader( composition );

    __setup_movie_composition_element( composition );    

    if( __setup_movie_node_track_matte2( composition ) == AE_FALSE )
    {
        return AE_NULL;
    }

    return composition;
}
//////////////////////////////////////////////////////////////////////////
static void __delete_nodes( const aeMovieComposition * _composition )
{
    const aeMovieNode *it_node = _composition->nodes;
    const aeMovieNode *it_node_end = _composition->nodes + _composition->node_count;
    for( ; it_node != it_node_end; ++it_node )
    {
        const aeMovieNode * node = it_node;

        const aeMovieLayerData * layer = node->layer;

        if( node->shader_data != AE_NULL )
        { 
            aeMovieShaderDeleterCallbackData callbackData;
            callbackData.element = node->shader_data;
            callbackData.name = layer->shader->name;
            callbackData.version = layer->shader->version;

            (*_composition->providers.shader_deleter)(&callbackData, _composition->provider_data);
        }

        if( layer->is_track_matte == AE_TRUE )
        {
            aeMovieTrackMatteDeleterCallbackData callbackData;
            callbackData.element = node->track_matte_data;
            callbackData.type = layer->type;
            
            (*_composition->providers.track_matte_deleter)(&callbackData, _composition->provider_data);
        }

        const aeMovieLayerData * track_matte_layer = node->track_matte_node == AE_NULL ? AE_NULL : node->track_matte_node->layer;

        aeMovieNodeDeleterCallbackData callbackData;
        callbackData.element = node->element_data;
        callbackData.layer = layer;
        callbackData.track_matte_layer = track_matte_layer;

        (*_composition->providers.node_deleter)(&callbackData, _composition->provider_data);
    }
}
//////////////////////////////////////////////////////////////////////////
static void __delete_camera( const aeMovieComposition * _composition )
{
    if( _composition->camera_data == AE_NULL )
    {
        return;
    }

    const aeMovieCompositionData * composition_data = _composition->composition_data;
    const aeMovieCompositionCamera * camera = composition_data->camera;

    aeMovieCameraDeleterCallbackData callbackData;
    callbackData.name = camera->name;
    callbackData.element = _composition->camera_data;

    (*_composition->providers.camera_deleter)(&callbackData, _composition->provider_data);
}
///
//////////////////////////////////////////////////////////////////////////
void ae_delete_movie_composition( const aeMovieComposition * _composition )
{
    __delete_nodes( _composition );
    __delete_camera( _composition );
    
    const aeMovieInstance * instance = _composition->movie_data->instance;

    const aeMovieSubComposition *it_subcomposition = _composition->subcompositions;
    const aeMovieSubComposition *it_subcomposition_end = _composition->subcompositions + _composition->subcomposition_count;
    for( ; it_subcomposition != it_subcomposition_end; ++it_subcomposition )
    {
        const aeMovieSubComposition * subcomposition = it_subcomposition;

        AE_DELETE( instance, subcomposition->animation );
    }

    AE_DELETE( instance, _composition->subcompositions );

    AE_DELETE( instance, _composition->nodes );

    AE_DELETE( instance, _composition->update_revision );

    AE_DELETE( instance, _composition );
}
//////////////////////////////////////////////////////////////////////////
const aeMovieCompositionData * ae_get_movie_composition_composition_data( const aeMovieComposition * _composition )
{
    const aeMovieCompositionData * composition_data = _composition->composition_data;

    return composition_data;
}
//////////////////////////////////////////////////////////////////////////
ae_bool_t ae_get_movie_composition_anchor_point( const aeMovieComposition * _composition, ae_vector3_t _point )
{
    const aeMovieCompositionData * composition_data = _composition->composition_data;

    if( composition_data->flags & AE_MOVIE_COMPOSITION_ANCHOR_POINT )
    {
        _point[0] = composition_data->anchor_point[0];
        _point[1] = composition_data->anchor_point[1];
        _point[2] = composition_data->anchor_point[2];

        return AE_TRUE;
    }

    return AE_FALSE;
}
//////////////////////////////////////////////////////////////////////////
ae_uint32_t ae_get_movie_composition_max_render_node( const aeMovieComposition * _composition )
{
    ae_uint32_t max_render_node = 0;

    ae_uint32_t node_count = _composition->node_count;

    ae_uint32_t iterator = 0;
    for( ; iterator != node_count; ++iterator )
    {
        const aeMovieNode * node = _composition->nodes + iterator;

        const aeMovieLayerData * layer = node->layer;

        if( layer->renderable == AE_FALSE )
        {
            continue;
        }

        ++max_render_node;
    }

    return max_render_node;
}
//////////////////////////////////////////////////////////////////////////
void ae_set_movie_composition_loop( const aeMovieComposition * _composition, ae_bool_t _loop )
{
    aeMovieCompositionAnimation * animation = _composition->animation;

    animation->loop = _loop;
}
//////////////////////////////////////////////////////////////////////////
ae_bool_t ae_get_movie_composition_loop( const  aeMovieComposition * _composition )
{
    const aeMovieCompositionAnimation * animation = _composition->animation;

    ae_bool_t loop = animation->loop;

    return loop;
}
//////////////////////////////////////////////////////////////////////////
ae_bool_t ae_get_movie_composition_interpolate( const aeMovieComposition * _composition )
{
    ae_bool_t interpolate = _composition->interpolate;

    return interpolate;
}
//////////////////////////////////////////////////////////////////////////
ae_bool_t ae_set_movie_composition_work_area( const aeMovieComposition * _composition, ae_float_t _begin, ae_float_t _end )
{
    ae_float_t duration = _composition->composition_data->duration;

    if( _begin < 0.f || _end < 0.f || _begin > duration || _end > duration || _begin > _end )
    {
        return AE_FALSE;
    }

    aeMovieCompositionAnimation * animation = _composition->animation;

    animation->work_area_begin = _begin;
    animation->work_area_end = _end;

    if( animation->time < _begin || animation->time >= _end )
    {
        ae_set_movie_composition_time( _composition, _begin );
    }

    return AE_TRUE;
}
//////////////////////////////////////////////////////////////////////////
void ae_remove_movie_composition_work_area( const  aeMovieComposition * _composition )
{
    aeMovieCompositionAnimation * animation = _composition->animation;

    animation->work_area_begin = 0.f;
    animation->work_area_end = _composition->composition_data->duration;

    ae_set_movie_composition_time( _composition, 0.f );
}
//////////////////////////////////////////////////////////////////////////
void ae_play_movie_composition( const aeMovieComposition * _composition, ae_float_t _time )
{
    aeMovieCompositionAnimation * animation = _composition->animation;

    if( animation->play == AE_TRUE )
    {
        return;
    }

    if( _time >= 0.f )
    {
        ae_float_t work_time = ae_minimax_f_f( _time, animation->work_area_begin, animation->work_area_end );

        ae_set_movie_composition_time( _composition, work_time );
    }

    animation->play = AE_TRUE;
    animation->pause = AE_FALSE;
    animation->interrupt = AE_FALSE;

    aeMovieCompositionStateCallbackData callbackData;
    callbackData.state = AE_MOVIE_COMPOSITION_PLAY;
    callbackData.subcomposition = AE_NULL;

    (_composition->providers.composition_state)(&callbackData, _composition->provider_data);
}
//////////////////////////////////////////////////////////////////////////
ae_bool_t ae_is_play_movie_composition( const aeMovieComposition * _composition )
{
    const aeMovieCompositionAnimation * animation = _composition->animation;

    ae_bool_t play = animation->play;

    return play;
}
//////////////////////////////////////////////////////////////////////////
ae_bool_t ae_is_pause_movie_composition( const aeMovieComposition * _composition )
{
    const aeMovieCompositionAnimation * animation = _composition->animation;

    ae_bool_t pause = animation->pause;

    return pause;
}
//////////////////////////////////////////////////////////////////////////
ae_bool_t ae_is_interrupt_movie_composition( const aeMovieComposition * _composition )
{
    const aeMovieCompositionAnimation * animation = _composition->animation;

    ae_bool_t interrupt = animation->interrupt;

    return interrupt;
}
//////////////////////////////////////////////////////////////////////////
static void __notify_stop_nodies( const aeMovieComposition * _composition, const aeMovieCompositionData * _compositionData, aeMovieCompositionAnimation * _animation, const aeMovieSubComposition * _subcomposition )
{
    __setup_movie_node_matrix2( _composition, _compositionData, _animation, _subcomposition );

    aeMovieNode *it_node = _composition->nodes;
    aeMovieNode *it_node_end = _composition->nodes + _composition->node_count;
    for( ; it_node != it_node_end; ++it_node )
    {
        aeMovieNode * node = it_node;

        if( node->subcomposition != _subcomposition )
        {
            continue;
        }

        const aeMovieLayerData * layer = node->layer;

        if( layer->is_track_matte == AE_TRUE )
        {
            if( node->animate != AE_MOVIE_NODE_ANIMATE_STATIC && node->animate != AE_MOVIE_NODE_ANIMATE_END )
            {
                aeMovieRenderMesh mesh;
                __compute_movie_node( _composition, node, &mesh, _composition->interpolate, AE_TRUE );

                aeMovieTrackMatteUpdateCallbackData callbackData;
                callbackData.element = node->element_data;
                callbackData.type = layer->type;
                callbackData.loop = _animation->loop;
                callbackData.state = AE_MOVIE_NODE_UPDATE_END;
                callbackData.offset = 0.f;
                callbackData.matrix = node->matrix;
                callbackData.opacity = node->opacity;
                callbackData.mesh = &mesh;
                callbackData.track_matte_data = node->track_matte_data;

                (*_composition->providers.track_matte_update)(&callbackData, _composition->provider_data);

                node->animate = AE_MOVIE_NODE_ANIMATE_STATIC;
            }
        }
        else
        {
            if( node->animate != AE_MOVIE_NODE_ANIMATE_STATIC && node->animate != AE_MOVIE_NODE_ANIMATE_END )
            {
                aeMovieNodeUpdateCallbackData callbackData;
                callbackData.element = node->element_data;
                callbackData.type = layer->type;
                callbackData.loop = _animation->loop;
                callbackData.state = AE_MOVIE_NODE_UPDATE_END;
                callbackData.offset = 0.f;
                callbackData.matrix = node->matrix;
                callbackData.opacity = node->opacity;

                (*_composition->providers.node_update)(&callbackData, _composition->provider_data);

                node->animate = AE_MOVIE_NODE_ANIMATE_STATIC;
            }
        }
    }
}
//////////////////////////////////////////////////////////////////////////
static void __notify_stop_nodies2( const aeMovieComposition * _composition )
{
    __notify_stop_nodies( _composition, _composition->composition_data, _composition->animation, AE_NULL );

    const aeMovieSubComposition *it_subcomposition = _composition->subcompositions;
    const aeMovieSubComposition *it_subcomposition_end = _composition->subcompositions + _composition->subcomposition_count;
    for( ; it_subcomposition != it_subcomposition_end; ++it_subcomposition )
    {
        const aeMovieSubComposition * subcomposition = it_subcomposition;

        __notify_stop_nodies( _composition, subcomposition->composition_data, subcomposition->animation, subcomposition );
    }
}
//////////////////////////////////////////////////////////////////////////
void ae_stop_movie_composition( const aeMovieComposition * _composition )
{
    aeMovieCompositionAnimation * animation = _composition->animation;

    if( animation->play == AE_FALSE )
    {
        return;
    }

    ae_set_movie_composition_time( _composition, animation->work_area_begin );

    animation->play = AE_FALSE;
    animation->pause = AE_FALSE;
    animation->interrupt = AE_FALSE;

    __notify_stop_nodies2( _composition );

    aeMovieCompositionStateCallbackData callbackData;

    callbackData.state = AE_MOVIE_COMPOSITION_STOP;
    callbackData.subcomposition = AE_NULL;

    (_composition->providers.composition_state)(&callbackData, _composition->provider_data);
}
//////////////////////////////////////////////////////////////////////////
static void __notify_pause_nodies( const aeMovieComposition * _composition, const aeMovieCompositionData * _compositionData, aeMovieCompositionAnimation * _animation, const aeMovieSubComposition * _subcomposition )
{
    __setup_movie_node_matrix2( _composition, _compositionData, _animation, _subcomposition );

    aeMovieNode *it_node = _composition->nodes;
    aeMovieNode *it_node_end = _composition->nodes + _composition->node_count;
    for( ; it_node != it_node_end; ++it_node )
    {
        aeMovieNode * node = it_node;

        if( node->subcomposition != _subcomposition )
        {
            continue;
        }

        const aeMovieLayerData * layer = node->layer;

        if( layer->is_track_matte == AE_TRUE )
        {
            if( node->animate != AE_MOVIE_NODE_ANIMATE_STATIC && node->animate != AE_MOVIE_NODE_ANIMATE_END )
            {
                aeMovieRenderMesh mesh;
                __compute_movie_node( _composition, node, &mesh, _composition->interpolate, AE_TRUE );

                aeMovieTrackMatteUpdateCallbackData callbackData;
                callbackData.element = node->element_data;
                callbackData.type = layer->type;
                callbackData.loop = _animation->loop;
                callbackData.state = AE_MOVIE_NODE_UPDATE_PAUSE;
                callbackData.offset = node->current_time;
                callbackData.matrix = node->matrix;
                callbackData.opacity = node->opacity;
                callbackData.mesh = &mesh;
                callbackData.track_matte_data = node->track_matte_data;

                (*_composition->providers.track_matte_update)(&callbackData, _composition->provider_data);
            }
        }
        else
        {
            if( node->animate != AE_MOVIE_NODE_ANIMATE_STATIC && node->animate != AE_MOVIE_NODE_ANIMATE_END )
            {
                aeMovieNodeUpdateCallbackData callbackData;
                callbackData.element = node->element_data;
                callbackData.type = layer->type;
                callbackData.loop = _animation->loop;
                callbackData.state = AE_MOVIE_NODE_UPDATE_PAUSE;
                callbackData.offset = node->current_time;
                callbackData.matrix = node->matrix;
                callbackData.opacity = node->opacity;

                (*_composition->providers.node_update)(&callbackData, _composition->provider_data);
            }
        }
    }
}
//////////////////////////////////////////////////////////////////////////
static void __notify_pause_nodies2( const aeMovieComposition * _composition )
{
    __notify_pause_nodies( _composition, _composition->composition_data, _composition->animation, AE_NULL );

    const aeMovieSubComposition *it_subcomposition = _composition->subcompositions;
    const aeMovieSubComposition *it_subcomposition_end = _composition->subcompositions + _composition->subcomposition_count;
    for( ; it_subcomposition != it_subcomposition_end; ++it_subcomposition )
    {
        const aeMovieSubComposition * subcomposition = it_subcomposition;

        __notify_pause_nodies( _composition, subcomposition->composition_data, subcomposition->animation, subcomposition );
    }
}
//////////////////////////////////////////////////////////////////////////
void ae_pause_movie_composition( const aeMovieComposition * _composition )
{
    aeMovieCompositionAnimation * animation = _composition->animation;

    if( animation->play == AE_FALSE )
    {
        return;
    }

    if( animation->pause == AE_TRUE )
    {
        return;
    }

    animation->pause = AE_TRUE;

    __notify_pause_nodies2( _composition );

    aeMovieCompositionStateCallbackData callbackData;

    callbackData.state = AE_MOVIE_COMPOSITION_PAUSE;
    callbackData.subcomposition = AE_NULL;

    (_composition->providers.composition_state)(&callbackData, _composition->provider_data);
}
//////////////////////////////////////////////////////////////////////////
static void __notify_resume_nodies( const aeMovieComposition * _composition, const aeMovieCompositionData * _compositionData, aeMovieCompositionAnimation * _animation, const aeMovieSubComposition * _subcomposition )
{
    __setup_movie_node_matrix2( _composition, _compositionData, _animation, _subcomposition );

    aeMovieNode *it_node = _composition->nodes;
    aeMovieNode *it_node_end = _composition->nodes + _composition->node_count;
    for( ; it_node != it_node_end; ++it_node )
    {
        aeMovieNode * node = it_node;

        if( node->subcomposition != _subcomposition )
        {
            continue;
        }

        const aeMovieLayerData * layer = node->layer;

        if( layer->is_track_matte == AE_TRUE )
        {
            if( node->animate != AE_MOVIE_NODE_ANIMATE_STATIC && node->animate != AE_MOVIE_NODE_ANIMATE_END )
            {
                aeMovieRenderMesh mesh;
                __compute_movie_node( _composition, node, &mesh, _composition->interpolate, AE_TRUE );

                aeMovieTrackMatteUpdateCallbackData callbackData;
                callbackData.element = node->element_data;
                callbackData.type = layer->type;
                callbackData.loop = _animation->loop;
                callbackData.state = AE_MOVIE_NODE_UPDATE_RESUME;
                callbackData.offset = node->current_time;
                callbackData.matrix = node->matrix;
                callbackData.opacity = node->opacity;
                callbackData.mesh = &mesh;
                callbackData.track_matte_data = node->track_matte_data;

                (*_composition->providers.track_matte_update)(&callbackData, _composition->provider_data);
            }
        }
        else
        {
            if( node->animate != AE_MOVIE_NODE_ANIMATE_STATIC && node->animate != AE_MOVIE_NODE_ANIMATE_END )
            {
                aeMovieNodeUpdateCallbackData callbackData;
                callbackData.element = node->element_data;
                callbackData.type = layer->type;
                callbackData.loop = _animation->loop;
                callbackData.state = AE_MOVIE_NODE_UPDATE_RESUME;
                callbackData.offset = node->current_time;
                callbackData.matrix = node->matrix;
                callbackData.opacity = node->opacity;

                (*_composition->providers.node_update)(&callbackData, _composition->provider_data);
            }
        }
    }
}
//////////////////////////////////////////////////////////////////////////
static void __notify_resume_nodies2( const aeMovieComposition * _composition )
{
    __notify_resume_nodies( _composition, _composition->composition_data, _composition->animation, AE_NULL );

    const aeMovieSubComposition *it_subcomposition = _composition->subcompositions;
    const aeMovieSubComposition *it_subcomposition_end = _composition->subcompositions + _composition->subcomposition_count;
    for( ; it_subcomposition != it_subcomposition_end; ++it_subcomposition )
    {
        const aeMovieSubComposition * subcomposition = it_subcomposition;

        __notify_resume_nodies( _composition, subcomposition->composition_data, subcomposition->animation, subcomposition );
    }
}
//////////////////////////////////////////////////////////////////////////
void ae_resume_movie_composition( const aeMovieComposition * _composition )
{
    aeMovieCompositionAnimation * animation = _composition->animation;

    if( animation->play == AE_FALSE )
    {
        return;
    }

    if( animation->pause == AE_FALSE )
    {
        return;
    }

    animation->pause = AE_FALSE;

    __notify_resume_nodies2( _composition );

    aeMovieCompositionStateCallbackData callbackData;

    callbackData.state = AE_MOVIE_COMPOSITION_RESUME;
    callbackData.subcomposition = AE_NULL;

    (_composition->providers.composition_state)(&callbackData, _composition->provider_data);
}
//////////////////////////////////////////////////////////////////////////
static ae_float_t __get_animation_loop_work_begin( const aeMovieCompositionAnimation * _animation )
{
    ae_bool_t loop = _animation->loop;

    if( loop == AE_TRUE )
    {
        ae_float_t work_begin = ae_max_f_f( _animation->loop_segment_begin, _animation->work_area_begin );

        return work_begin;
    }
    else
    {
        ae_float_t work_begin = _animation->work_area_begin;

        return work_begin;
    }
}
//////////////////////////////////////////////////////////////////////////
static ae_float_t __get_animation_loop_work_end( const aeMovieCompositionAnimation * _animation )
{
    ae_bool_t loop = _animation->loop;

    if( loop == AE_TRUE )
    {
        ae_float_t work_end = ae_min_f_f( _animation->loop_segment_end, _animation->work_area_end );

        return work_end;
    }
    else
    {
        ae_float_t work_end = _animation->work_area_end;

        return work_end;
    }
}
//////////////////////////////////////////////////////////////////////////
static void __update_movie_composition_node_state( const aeMovieComposition * _composition, aeMovieNode * _node, ae_bool_t _loop, ae_bool_t _begin, ae_float_t _time )
{
    if( _node->element_data == AE_NULL )
    {
        return;
    }

    const aeMovieLayerData * layer = _node->layer;

    if( _begin == AE_TRUE )
    {
        if( _node->animate == AE_MOVIE_NODE_ANIMATE_STATIC || _node->animate == AE_MOVIE_NODE_ANIMATE_END )
        {
            _node->animate = AE_MOVIE_NODE_ANIMATE_BEGIN;

            aeMovieNodeUpdateCallbackData callbackData;
            callbackData.element = _node->element_data;
            callbackData.type = layer->type;
            callbackData.loop = _loop;
            callbackData.state = AE_MOVIE_NODE_UPDATE_BEGIN;
            callbackData.offset = _node->start_time + _time - _node->in_time;
            callbackData.matrix = _node->matrix;
            callbackData.opacity = _node->opacity;

            (*_composition->providers.node_update)(&callbackData, _composition->provider_data);
        }
        else
        {
            _node->animate = AE_MOVIE_NODE_ANIMATE_PROCESS;

            aeMovieNodeUpdateCallbackData callbackData;
            callbackData.element = _node->element_data;
            callbackData.type = layer->type;
            callbackData.loop = _loop;
            callbackData.state = AE_MOVIE_NODE_UPDATE_UPDATE;
            callbackData.offset = 0.f;
            callbackData.matrix = _node->matrix;
            callbackData.opacity = _node->opacity;

            (*_composition->providers.node_update)(&callbackData, _composition->provider_data);
        }
    }
    else
    {
        if( _node->animate == AE_MOVIE_NODE_ANIMATE_PROCESS || _node->animate == AE_MOVIE_NODE_ANIMATE_BEGIN )
        {
            _node->animate = AE_MOVIE_NODE_ANIMATE_END;

            aeMovieNodeUpdateCallbackData callbackData;
            callbackData.element = _node->element_data;
            callbackData.type = layer->type;
            callbackData.loop = _loop;
            callbackData.state = AE_MOVIE_NODE_UPDATE_END;
            callbackData.offset = 0.f;
            callbackData.matrix = _node->matrix;
            callbackData.opacity = _node->opacity;

            (*_composition->providers.node_update)(&callbackData, _composition->provider_data);
        }
        else
        {
            _node->animate = AE_MOVIE_NODE_ANIMATE_STATIC;
        }
    }
}
//////////////////////////////////////////////////////////////////////////
static void __update_movie_composition_track_matte_state( const aeMovieComposition * _composition, aeMovieNode * _node, ae_bool_t _loop, ae_bool_t _begin, ae_float_t _time, ae_bool_t _interpolate )
{
    const aeMovieLayerData * layer = _node->layer;

    aeMovieLayerTypeEnum layer_type = layer->type;

    switch( layer_type )
    {
    case AE_MOVIE_LAYER_TYPE_MOVIE:
        {
            return;
        }break;
    default:
        {
        }break;
    }

    aeMovieRenderMesh mesh;
    __compute_movie_node( _composition, _node, &mesh, _interpolate, AE_TRUE );

    aeMovieTrackMatteUpdateCallbackData callbackData;
    callbackData.element = _node->element_data;
    callbackData.type = layer_type;
    callbackData.loop = _loop;
    callbackData.offset = _node->start_time + _time - _node->in_time;
    callbackData.matrix = _node->matrix;
    callbackData.opacity = 0.f;
    callbackData.mesh = &mesh;
    callbackData.track_matte_data = _node->track_matte_data;

    if( _begin == AE_TRUE )
    {
        if( _node->animate == AE_MOVIE_NODE_ANIMATE_STATIC || _node->animate == AE_MOVIE_NODE_ANIMATE_END )
        {
            _node->animate = AE_MOVIE_NODE_ANIMATE_BEGIN;

            callbackData.state = AE_MOVIE_NODE_UPDATE_BEGIN;

            (*_composition->providers.track_matte_update)(&callbackData, _composition->provider_data);
        }
        else
        {
            _node->animate = AE_MOVIE_NODE_ANIMATE_PROCESS;

            callbackData.state = AE_MOVIE_NODE_UPDATE_UPDATE;

            (*_composition->providers.track_matte_update)(&callbackData, _composition->provider_data);
        }
    }
    else
    {
        if( _node->animate == AE_MOVIE_NODE_ANIMATE_PROCESS || _node->animate == AE_MOVIE_NODE_ANIMATE_BEGIN )
        {
            _node->animate = AE_MOVIE_NODE_ANIMATE_END;

            callbackData.state = AE_MOVIE_NODE_UPDATE_END;

            (*_composition->providers.track_matte_update)(&callbackData, _composition->provider_data);
        }
        else
        {
            _node->animate = AE_MOVIE_NODE_ANIMATE_STATIC;
        }
    }
}
//////////////////////////////////////////////////////////////////////////
static void __update_node( const aeMovieComposition * _composition, const aeMovieCompositionData * _compositionData, const aeMovieCompositionAnimation * _animation, aeMovieNode * _node, ae_uint32_t _revision, ae_float_t _time, ae_uint32_t _frameId, ae_float_t _t, ae_bool_t _loop, ae_bool_t _interpolate, ae_bool_t _begin )
{
#	ifdef AE_MOVIE_DEBUG	
    if( __test_error_composition_layer_frame( _composition->movie_data->instance
        , _compositionData
        , _node->layer
        , _frameId
        , "__update_node frame id out count"
    ) == AE_FALSE )
    {
        return;
    }
#	endif

    __update_movie_composition_node_matrix( _composition, _compositionData, _animation, _node, _revision, _frameId, _interpolate, _t );

    if( _node->shader_data != AE_NULL )
    {
        __update_movie_composition_node_shader( _composition, _compositionData, _node, _revision, _frameId, _interpolate, _t );
    }

    if( _node->layer->is_track_matte == AE_TRUE )
    {
        __update_movie_composition_track_matte_state( _composition, _node, _loop, _begin, _time, _interpolate );
    }
    else
    {
        __update_movie_composition_node_state( _composition, _node, _loop, _begin, _time );
    }

    _node->update_revision = _revision;
}
//////////////////////////////////////////////////////////////////////////
static void __update_movie_composition_node( const aeMovieComposition * _composition, const aeMovieCompositionData * _compositionData, const aeMovieCompositionAnimation * _animation, const aeMovieSubComposition * _subcomposition, ae_uint32_t _revision, ae_float_t _beginTime, ae_float_t _endTime )
{
    ae_bool_t composition_interpolate = _composition->interpolate;

    ae_bool_t animation_interrupt = _animation->interrupt;
    ae_bool_t animation_loop = _animation->loop;
    
    ae_float_t loopBegin = __get_animation_loop_work_begin( _animation );
    ae_float_t loopEnd = __get_animation_loop_work_end( _animation );
    
    aeMovieNode *it_node = _composition->nodes;
    aeMovieNode *it_node_end = _composition->nodes + _composition->node_count;
    for( ; it_node != it_node_end; ++it_node )
    {
        aeMovieNode * node = it_node;

        if( node->ignore == AE_TRUE )
        {
            continue;
        }

        if( node->subcomposition != _subcomposition )
        {
            continue;
        }

        const aeMovieLayerData * layer = node->layer;

        ae_float_t frameDurationInv = layer->composition_data->frameDurationInv;

        ae_bool_t test_time = (_beginTime >= loopBegin
            && _endTime < loopEnd
            && animation_interrupt == AE_FALSE
            && animation_loop == AE_TRUE
            && layer->type != AE_MOVIE_LAYER_TYPE_EVENT);

        ae_float_t in_time = (test_time == AE_TRUE && node->in_time <= loopBegin) ? loopBegin : node->in_time;
        ae_float_t out_time = (test_time == AE_TRUE && node->out_time >= loopEnd) ? loopEnd : node->out_time;

        ae_uint32_t beginFrame = (ae_uint32_t)(_beginTime * frameDurationInv + 0.001f);
        ae_uint32_t endFrame = (ae_uint32_t)(_endTime * frameDurationInv + 0.001f);
        ae_uint32_t indexIn = (ae_uint32_t)(in_time * frameDurationInv + 0.001f);
        ae_uint32_t indexOut = (ae_uint32_t)(out_time * frameDurationInv + 0.001f);

        if( indexIn > endFrame || indexOut < beginFrame )
        {
            node->active = AE_FALSE;

            continue;
        }

        ae_float_t current_time = (endFrame >= indexOut) ? out_time - node->in_time + node->start_time : _animation->time - node->in_time + node->start_time;
        ae_float_t frame_time = current_time / node->stretch * frameDurationInv;

        ae_uint32_t frameId = (ae_uint32_t)frame_time;

        if( layer->type == AE_MOVIE_LAYER_TYPE_EVENT )
        {
            node->current_time = current_time;

            __update_movie_composition_node_matrix( _composition, _compositionData, _animation, node, _revision, frameId, AE_FALSE, 0.f );

            if( beginFrame < indexIn && endFrame >= indexIn )
            {
                aeMovieCompositionEventCallbackData callbackData;
                callbackData.element = node->element_data;
                callbackData.name = layer->name;
                callbackData.matrix = node->matrix;
                callbackData.opacity = node->opacity;
                callbackData.begin = AE_TRUE;

                (*_composition->providers.composition_event)(&callbackData, _composition->provider_data);
            }

            if( beginFrame < indexOut && endFrame >= indexOut )
            {
                aeMovieCompositionEventCallbackData callbackData;
                callbackData.element = node->element_data;
                callbackData.name = layer->name;
                callbackData.matrix = node->matrix;
                callbackData.opacity = node->opacity;
                callbackData.begin = AE_FALSE;

                (*_composition->providers.composition_event)(&callbackData, _composition->provider_data);
            }
        }
        else
        {
            if( indexIn >= beginFrame && indexOut < endFrame )
            {
                node->active = AE_FALSE;

                continue;
            }

            node->current_time = current_time;

            ae_bool_t node_loop = ((animation_loop == AE_TRUE && animation_interrupt == AE_FALSE && loopBegin >= node->in_time && node->out_time >= loopEnd) || (layer->params & AE_MOVIE_LAYER_PARAM_LOOP)) ? AE_TRUE : AE_FALSE;

            if( beginFrame < indexIn && endFrame >= indexIn && endFrame < indexOut )
            {
                node->active = AE_TRUE;

                ae_bool_t node_interpolate = composition_interpolate ? (node_loop == AE_TRUE ? (endFrame + 1) < indexOut : AE_FALSE) : AE_FALSE;
                ae_float_t t = node_interpolate == AE_TRUE ? frame_time - (ae_float_t)frameId : 0.f;

                __update_node( _composition, _compositionData, _animation, node, _revision, _endTime, frameId, t, node_loop, node_interpolate, AE_TRUE );
            }
            else if( endFrame >= indexOut && beginFrame >= indexIn && beginFrame < indexOut )
            {
                ae_bool_t node_deactive = (node_loop == AE_TRUE) ? AE_TRUE : AE_FALSE;

                node->active = node_deactive;

                ae_uint32_t frameEnd = indexOut - indexIn;

                __update_node( _composition, _compositionData, _animation, node, _revision, _endTime, frameEnd, 0.f, node_loop, AE_FALSE, AE_FALSE );
            }
            else if( beginFrame >= indexIn && endFrame >= indexIn && endFrame < indexOut )
            {
                node->active = AE_TRUE;

                ae_bool_t node_interpolate = composition_interpolate ? (node_loop == AE_TRUE ? (endFrame + 1) < indexOut : AE_FALSE) : AE_FALSE;
                ae_float_t t = node_interpolate == AE_TRUE ? frame_time - (ae_float_t)frameId : 0.f;

                __update_node( _composition, _compositionData, _animation, node, _revision, _endTime, frameId, t, node_loop, node_interpolate, AE_TRUE );
            }
        }
    }
}
//////////////////////////////////////////////////////////////////////////
static void __update_movie_camera( const aeMovieComposition * _composition, const aeMovieCompositionAnimation * _animation )
{
    if( _composition->camera_data == AE_NULL )
    {
        return;
    }

    const aeMovieCompositionData * composition_data = _composition->composition_data;

    ae_bool_t composition_interpolate = _composition->interpolate;

    aeMovieCameraUpdateCallbackData callbackData;
    callbackData.element = _composition->camera_data;
    
    ae_float_t frameDurationInv = composition_data->frameDurationInv;

    ae_float_t frame_time = _animation->time * frameDurationInv;

    ae_uint32_t frame_id = (ae_uint32_t)frame_time;

    if( composition_interpolate == AE_FALSE )
    {   
        ae_movie_make_camera_transformation( callbackData.target, callbackData.position, callbackData.quaternion, composition_data->camera, frame_id, AE_FALSE, 0.f );
    }
    else
    {
        ae_float_t t = frame_time - (ae_float_t)frame_id;

        ae_movie_make_camera_transformation( callbackData.target, callbackData.position, callbackData.quaternion, composition_data->camera, frame_id, AE_TRUE, t );
    }

    (*_composition->providers.camera_update)(&callbackData, _composition->provider_data);
}
//////////////////////////////////////////////////////////////////////////
static void __skip_movie_composition_node( const aeMovieComposition * _composition, const aeMovieCompositionData * _compositionData, aeMovieCompositionAnimation * _animation, const aeMovieSubComposition * _subcomposition, ae_uint32_t _revision, ae_float_t _beginTime, ae_float_t _endTime )
{
    aeMovieNode	*it_node = _composition->nodes;
    aeMovieNode *it_node_end = _composition->nodes + _composition->node_count;
    for( ; it_node != it_node_end; ++it_node )
    {
        aeMovieNode * node = it_node;

        if( node->ignore == AE_TRUE )
        {
            continue;
        }

        if( node->subcomposition != _subcomposition )
        {
            continue;
        }

        const aeMovieLayerData * layer = node->layer;

        ae_float_t frameDurationInv = layer->composition_data->frameDurationInv;

        ae_float_t in_time = node->in_time;
        ae_float_t out_time = node->out_time;

        ae_uint32_t beginFrame = (ae_uint32_t)(_beginTime * frameDurationInv + 0.001f);
        ae_uint32_t endFrame = (ae_uint32_t)(_endTime * frameDurationInv + 0.001f);
        ae_uint32_t indexIn = (ae_uint32_t)(in_time * frameDurationInv + 0.001f);
        ae_uint32_t indexOut = (ae_uint32_t)(out_time * frameDurationInv + 0.001f);

        if( indexIn > endFrame || indexOut < beginFrame )
        {
            node->active = AE_FALSE;

            continue;
        }

        if( indexIn >= beginFrame && indexOut < endFrame )
        {
            node->active = AE_FALSE;

            continue;
        }

        ae_float_t current_time = (endFrame >= indexOut) ? out_time - node->in_time + node->start_time : _animation->time - node->in_time + node->start_time;
        ae_float_t frame_time = current_time / node->stretch * frameDurationInv;

        ae_uint32_t frameId = (ae_uint32_t)frame_time;

        node->current_time = current_time;

        if( layer->type == AE_MOVIE_LAYER_TYPE_EVENT )
        {
        }
        else
        {
            if( beginFrame < indexIn && endFrame >= indexIn && endFrame < indexOut )
            {
            }
            else if( endFrame >= indexOut && beginFrame >= indexIn && beginFrame < indexOut )
            {
                node->active = AE_FALSE;

                ae_float_t t = frame_time - (ae_float_t)frameId;

                __update_node( _composition, _compositionData, _animation, node, _revision, _endTime, frameId, t, AE_FALSE, (endFrame + 1) < indexOut, AE_FALSE );
            }
            else if( beginFrame >= indexIn && endFrame >= indexIn && endFrame < indexOut )
            {
            }
        }

        node->animate = AE_MOVIE_NODE_ANIMATE_STATIC;
    }
}
//////////////////////////////////////////////////////////////////////////
static void __update_movie_subcomposition( aeMovieComposition * _composition, const aeMovieCompositionData * _compositionData, ae_float_t _timing, aeMovieCompositionAnimation * _animation, const aeMovieSubComposition * _subcomposition )
{
    ae_uint32_t update_revision = __get_composition_update_revision( _composition );

    ae_float_t prev_time = _animation->time;

    ae_float_t begin_time = prev_time;

    if( _animation->play == AE_TRUE )
    {
        ae_float_t duration = _animation->work_area_end - _animation->work_area_begin;

        ae_float_t frameDuration = _composition->composition_data->frameDuration;

        if( _animation->loop == AE_FALSE || _animation->interrupt == AE_TRUE )
        {
            ae_float_t last_time = duration - frameDuration;

            if( _animation->time + _timing >= last_time )
            {
                _animation->time = last_time;

                __update_movie_composition_node( _composition, _compositionData, _animation, _subcomposition, update_revision, begin_time, last_time );

                update_revision = __inc_composition_update_revision( _composition );

                _animation->time = last_time;
                _animation->play = AE_FALSE;
                _animation->pause = AE_FALSE;
                _animation->interrupt = AE_FALSE;

                if( _subcomposition == AE_NULL )
                {
                    aeMovieCompositionStateCallbackData callbackData;
                    callbackData.state = AE_MOVIE_COMPOSITION_END;
                    callbackData.subcomposition = AE_NULL;

                    (*_composition->providers.composition_state)(&callbackData, _composition->provider_data);
                }
                else
                {
                    aeMovieCompositionStateCallbackData callbackData;
                    callbackData.state = AE_MOVIE_SUB_COMPOSITION_END;
                    callbackData.subcomposition = _subcomposition;

                    (*_composition->providers.composition_state)(&callbackData, _composition->provider_data);
                }

                return;
            }
            else
            {
                _animation->time += _timing;
            }
        }
        else
        {
            ae_float_t loopBegin = ae_max_f_f( _animation->loop_segment_begin, _animation->work_area_begin );
            ae_float_t loopEnd = ae_min_f_f( _animation->loop_segment_end, _animation->work_area_end );

            ae_float_t last_time = loopEnd - frameDuration;

            if( _animation->time + _timing >= last_time )
            {
                ae_float_t new_composition_time = _animation->time + _timing - last_time + loopBegin;

                while( new_composition_time >= last_time )
                {
                    new_composition_time -= last_time;
                    new_composition_time += loopBegin;
                }

                _animation->time = last_time;

                __update_movie_composition_node( _composition, _compositionData, _animation, _subcomposition, update_revision, begin_time, last_time );

                update_revision = __inc_composition_update_revision( _composition );

                begin_time = loopBegin;

                _animation->time = new_composition_time;

                if( _subcomposition == AE_NULL )
                {
                    aeMovieCompositionStateCallbackData callbackData;
                    callbackData.state = AE_MOVIE_COMPOSITION_LOOP_END;
                    callbackData.subcomposition = AE_NULL;

                    (*_composition->providers.composition_state)(&callbackData, _composition->provider_data);
                }
                else
                {
                    aeMovieCompositionStateCallbackData callbackData;
                    callbackData.state = AE_MOVIE_SUB_COMPOSITION_LOOP_END;
                    callbackData.subcomposition = _subcomposition;

                    (*_composition->providers.composition_state)(&callbackData, _composition->provider_data);
                }
            }
            else
            {
                _animation->time += _timing;
            }
        }
    }

    __update_movie_composition_node( _composition, _compositionData, _animation, _subcomposition, update_revision, begin_time, _animation->time );    
}
//////////////////////////////////////////////////////////////////////////
void ae_update_movie_composition( aeMovieComposition * _composition, ae_float_t _timing )
{
    __inc_composition_update_revision( _composition );

    aeMovieCompositionAnimation * animation = _composition->animation;
    const aeMovieCompositionData * composition_data = _composition->composition_data;

    if( animation->play == AE_TRUE && animation->pause == AE_FALSE )
    {
        __update_movie_subcomposition( _composition, composition_data, _timing, animation, AE_NULL );
        __update_movie_camera( _composition, animation );
    }

    const aeMovieSubComposition *it_subcomposition = _composition->subcompositions;
    const aeMovieSubComposition *it_subcomposition_end = _composition->subcompositions + _composition->subcomposition_count;
    for( ; it_subcomposition != it_subcomposition_end; ++it_subcomposition )
    {
        const aeMovieSubComposition * subcomposition = it_subcomposition;

        aeMovieCompositionAnimation * subcomposition_animation = subcomposition->animation;

        ae_float_t subcomposition_timing = _timing;

        if( subcomposition_animation->play == AE_FALSE || subcomposition_animation->pause == AE_TRUE )
        {
            subcomposition_timing = 0.f;
        }

        __update_movie_subcomposition( _composition, subcomposition->composition_data, subcomposition_timing, subcomposition_animation, subcomposition );
    }
}
//////////////////////////////////////////////////////////////////////////
void __set_movie_composition_time( const aeMovieComposition * _composition, const aeMovieCompositionData * _compositionData, aeMovieCompositionAnimation * _animation, ae_float_t _time, const aeMovieSubComposition * _subcomposition )
{
    ae_float_t duration = _compositionData->duration;

    if( _time < 0.f && _time > duration )
    {
        return;
    }

    if( ae_equal_f_f( _animation->time, _time ) == AE_TRUE )
    {
        return;
    }

    ae_uint32_t update_revision = __inc_composition_update_revision( _composition );

    if( _animation->time > _time )
    {
        __skip_movie_composition_node( _composition, _compositionData, _animation, _subcomposition, update_revision, _animation->time, _composition->composition_data->duration );

        update_revision = __inc_composition_update_revision( _composition );

        _animation->time = _time;

        __update_movie_composition_node( _composition, _compositionData, _animation, _subcomposition, update_revision, 0.f, _time );
    }
    else
    {
        _animation->time = _time;

        __update_movie_composition_node( _composition, _compositionData, _animation, _subcomposition, update_revision, _animation->time, _time );
    }
}
//////////////////////////////////////////////////////////////////////////
void ae_interrupt_movie_composition( const aeMovieComposition * _composition, ae_bool_t _skip )
{
    aeMovieCompositionAnimation * animation = _composition->animation;

    if( animation->play == AE_FALSE )
    {
        return;
    }

    if( animation->pause == AE_TRUE )
    {
        return;
    }

    if( animation->interrupt == AE_TRUE )
    {
        return;
    }

    animation->interrupt = AE_TRUE;

    if( _skip == AE_TRUE )
    {
        ae_float_t loop_work_end = __get_animation_loop_work_end( animation );

        if( animation->time < loop_work_end )
        {
            const aeMovieCompositionData * composition_data = _composition->composition_data;

            __set_movie_composition_time( _composition, composition_data, animation, loop_work_end, AE_NULL );
        }
    }

    aeMovieCompositionStateCallbackData callbackData;

    callbackData.state = AE_MOVIE_COMPOSITION_INTERRUPT;
    callbackData.subcomposition = AE_NULL;

    (_composition->providers.composition_state)(&callbackData, _composition->provider_data);
}
//////////////////////////////////////////////////////////////////////////
void ae_set_movie_composition_time( const aeMovieComposition * _composition, ae_float_t _time )
{
    const aeMovieCompositionData * composition_data = _composition->composition_data;
    aeMovieCompositionAnimation * animation = _composition->animation;

    __set_movie_composition_time( _composition, composition_data, animation, _time, AE_NULL );
}
//////////////////////////////////////////////////////////////////////////
const ae_char_t * ae_get_movie_composition_name( const aeMovieComposition * _composition )
{
    const aeMovieCompositionData * composition_data = _composition->composition_data;

    const ae_char_t * name = composition_data->name;

    return name;
}
//////////////////////////////////////////////////////////////////////////
ae_float_t ae_get_movie_composition_time( const aeMovieComposition * _composition )
{
    const aeMovieCompositionAnimation * animation = _composition->animation;

    ae_float_t time = animation->time;

    return time;
}
//////////////////////////////////////////////////////////////////////////
ae_float_t ae_get_movie_composition_duration( const aeMovieComposition * _composition )
{
    ae_float_t duration = ae_get_movie_composition_data_duration( _composition->composition_data );

    return duration;
}
//////////////////////////////////////////////////////////////////////////
void ae_get_movie_composition_in_out_loop( const aeMovieComposition * _composition, ae_float_t * _in, ae_float_t * _out )
{
    aeMovieCompositionAnimation * animation = _composition->animation;
    ae_float_t work_begin = ae_max_f_f( animation->loop_segment_begin, animation->work_area_begin );
    ae_float_t work_end = ae_min_f_f( animation->loop_segment_end, animation->work_area_end );

    *_in = work_begin;
    *_out = work_end;
}
//////////////////////////////////////////////////////////////////////////
ae_bool_t ae_set_movie_composition_slot( const aeMovieComposition * _composition, const ae_char_t * _slotName, ae_voidptr_t _slotData )
{
    const aeMovieInstance * instance = _composition->movie_data->instance;

    aeMovieNode *it_node = _composition->nodes;
    aeMovieNode *it_node_end = _composition->nodes + _composition->node_count;
    for( ; it_node != it_node_end; ++it_node )
    {
        aeMovieNode * node = it_node;

        const aeMovieLayerData * layer = node->layer;

        if( layer->type != AE_MOVIE_LAYER_TYPE_SLOT )
        {
            continue;
        }

        if( AE_STRNCMP( instance, layer->name, _slotName, AE_MOVIE_MAX_LAYER_NAME ) != 0 )
        {
            continue;
        }

        node->element_data = _slotData;

        return AE_TRUE;
    }

    return AE_FALSE;
}
//////////////////////////////////////////////////////////////////////////
ae_voidptr_t ae_get_movie_composition_slot( const aeMovieComposition * _composition, const ae_char_t * _slotName )
{
    const aeMovieInstance * instance = _composition->movie_data->instance;

    const aeMovieNode *it_node = _composition->nodes;
    const aeMovieNode *it_node_end = _composition->nodes + _composition->node_count;
    for( ; it_node != it_node_end; ++it_node )
    {
        const aeMovieNode * node = it_node;

        const aeMovieLayerData * layer = node->layer;

        if( layer->type != AE_MOVIE_LAYER_TYPE_SLOT )
        {
            continue;
        }

        if( AE_STRNCMP( instance, layer->name, _slotName, AE_MOVIE_MAX_LAYER_NAME ) != 0 )
        {
            continue;
        }

        return node->element_data;
    }

    return AE_NULL;
}
//////////////////////////////////////////////////////////////////////////
ae_bool_t ae_has_movie_composition_slot( const aeMovieComposition * _composition, const ae_char_t * _slotName )
{
    const aeMovieInstance * instance = _composition->movie_data->instance;

    const aeMovieNode *it_node = _composition->nodes;
    const aeMovieNode *it_node_end = _composition->nodes + _composition->node_count;
    for( ; it_node != it_node_end; ++it_node )
    {
        const aeMovieNode * node = it_node;

        const aeMovieLayerData * layer = node->layer;

        if( layer->type != AE_MOVIE_LAYER_TYPE_SLOT )
        {
            continue;
        }

        if( AE_STRNCMP( instance, layer->name, _slotName, AE_MOVIE_MAX_LAYER_NAME ) != 0 )
        {
            continue;
        }

        return AE_TRUE;
    }

    return AE_FALSE;
}
//////////////////////////////////////////////////////////////////////////
ae_voidptr_t ae_remove_movie_composition_slot( const aeMovieComposition * _composition, const ae_char_t * _slotName )
{
    const aeMovieInstance * instance = _composition->movie_data->instance;

    aeMovieNode *it_node = _composition->nodes;
    aeMovieNode *it_node_end = _composition->nodes + _composition->node_count;
    for( ; it_node != it_node_end; ++it_node )
    {
        aeMovieNode * node = it_node;

        const aeMovieLayerData * layer = node->layer;

        if( layer->type != AE_MOVIE_LAYER_TYPE_SLOT )
        {
            continue;
        }

        if( AE_STRNCMP( instance, layer->name, _slotName, AE_MOVIE_MAX_LAYER_NAME ) != 0 )
        {
            continue;
        }

        ae_voidptr_t prev_element_data = node->element_data;

        node->element_data = AE_NULL;

        return prev_element_data;
    }

    return AE_NULL;
}
//////////////////////////////////////////////////////////////////////////
ae_voidptr_t ae_get_movie_composition_camera_data( const aeMovieComposition * _composition )
{
    return _composition->camera_data;
}
//////////////////////////////////////////////////////////////////////////
ae_bool_t ae_get_movie_composition_socket( const aeMovieComposition * _composition, const ae_char_t * _slotName, const aeMoviePolygon ** _polygon )
{
    const aeMovieInstance * instance = _composition->movie_data->instance;

    const aeMovieNode *it_node = _composition->nodes;
    const aeMovieNode *it_node_end = _composition->nodes + _composition->node_count;
    for( ; it_node != it_node_end; ++it_node )
    {
        const aeMovieNode * node = it_node;

        const aeMovieLayerData * layer = node->layer;

        if( layer->type != AE_MOVIE_LAYER_TYPE_SOCKET )
        {
            continue;
        }

        if( AE_STRNCMP( instance, layer->name, _slotName, AE_MOVIE_MAX_LAYER_NAME ) != 0 )
        {
            continue;
        }

        if( layer->polygon->immutable == AE_TRUE )
        {
            *_polygon = &layer->polygon->immutable_polygon;

            return AE_TRUE;
        }
        else
        {
            ae_uint32_t frame = __compute_movie_node_frame( node, AE_FALSE, AE_NULL );

            *_polygon = layer->polygon->polygons + frame;
        }
    }

    return AE_FALSE;
}
//////////////////////////////////////////////////////////////////////////
ae_bool_t ae_compute_movie_mesh( const aeMovieComposition * _composition, ae_uint32_t * _iterator, aeMovieRenderMesh * _render )
{
    ae_bool_t composition_interpolate = _composition->interpolate;

    ae_uint32_t render_node_index = *_iterator;
    ae_uint32_t render_node_max_count = _composition->node_count;

    ae_uint32_t iterator = render_node_index;
    for( ; iterator != render_node_max_count; ++iterator )
    {
        const aeMovieNode * node = _composition->nodes + iterator;

        if( node->active == AE_FALSE )
        {
            continue;
        }

        if( node->enable == AE_FALSE )
        {
            continue;
        }

        const aeMovieLayerData * layer = node->layer;

        if( layer->renderable == AE_FALSE )
        {
            continue;
        }

        if( node->track_matte_node != AE_NULL && node->track_matte_node->active == AE_FALSE )
        {
            continue;
        }

        *_iterator = iterator + 1;

        __compute_movie_node( _composition, node, _render, composition_interpolate, AE_FALSE );

        return AE_TRUE;
    }

    return AE_FALSE;
}
//////////////////////////////////////////////////////////////////////////
ae_uint32_t ae_get_movie_render_mesh_count( const aeMovieComposition * _composition )
{
    ae_uint32_t count = 0;

    ae_uint32_t render_node_max_count = _composition->node_count;

    ae_uint32_t iterator = 0;
    for( ; iterator != render_node_max_count; ++iterator )
    {
        const aeMovieNode * node = _composition->nodes + iterator;

        if( node->active == AE_FALSE )
        {
            continue;
        }

        const aeMovieLayerData * layer = node->layer;

        if( layer->renderable == AE_FALSE )
        {
            continue;
        }

        if( node->track_matte_node != AE_NULL && node->track_matte_node->active == AE_FALSE )
        {
            continue;
        }

        count += 1;
    }

    return count;
}
//////////////////////////////////////////////////////////////////////////
ae_bool_t ae_get_movie_composition_node_in_out_time( const aeMovieComposition * _composition, const ae_char_t * _layerName, aeMovieLayerTypeEnum _type, ae_float_t * _in, ae_float_t * _out )
{
    const aeMovieInstance * instance = _composition->movie_data->instance;

    const aeMovieNode *it_node = _composition->nodes;
    const aeMovieNode *it_node_end = _composition->nodes + _composition->node_count;
    for( ; it_node != it_node_end; ++it_node )
    {
        const aeMovieNode * node = it_node;

        const aeMovieLayerData * layer = node->layer;

        if( _type != AE_MOVIE_LAYER_TYPE_ANY
            && layer->type != _type )
        {
            continue;
        }

        if( AE_STRNCMP( instance, layer->name, _layerName, AE_MOVIE_MAX_LAYER_NAME ) != 0 )
        {
            continue;
        }

        *_in = node->in_time;
        *_out = node->out_time;

        return AE_TRUE;
    }

    return AE_FALSE;
}
//////////////////////////////////////////////////////////////////////////
ae_bool_t ae_set_movie_composition_node_enable( const aeMovieComposition * _composition, const ae_char_t * _layerName, aeMovieLayerTypeEnum _type, ae_bool_t _enable )
{
    const aeMovieInstance * instance = _composition->movie_data->instance;

    aeMovieNode *it_node = _composition->nodes;
    aeMovieNode *it_node_end = _composition->nodes + _composition->node_count;
    for( ; it_node != it_node_end; ++it_node )
    {
        aeMovieNode * node = it_node;

        const aeMovieLayerData * layer = node->layer;

        if( _type != AE_MOVIE_LAYER_TYPE_ANY
            && layer->type != _type )
        {
            continue;
        }

        if( AE_STRNCMP( instance, layer->name, _layerName, AE_MOVIE_MAX_LAYER_NAME ) != 0 )
        {
            continue;
        }

        node->enable = _enable;

        return AE_TRUE;
    }

    return AE_FALSE;
}
//////////////////////////////////////////////////////////////////////////
ae_bool_t ae_get_movie_composition_node_enable( const aeMovieComposition * _composition, const ae_char_t * _layerName, aeMovieLayerTypeEnum _type, ae_bool_t * _enable )
{
    const aeMovieInstance * instance = _composition->movie_data->instance;

    const aeMovieNode *it_node = _composition->nodes;
    const aeMovieNode *it_node_end = _composition->nodes + _composition->node_count;
    for( ; it_node != it_node_end; ++it_node )
    {
        const aeMovieNode * node = it_node;

        const aeMovieLayerData * layer = node->layer;

        if( _type != AE_MOVIE_LAYER_TYPE_ANY
            && layer->type != _type )
        {
            continue;
        }

        if( AE_STRNCMP( instance, layer->name, _layerName, AE_MOVIE_MAX_LAYER_NAME ) != 0 )
        {
            continue;
        }

        *_enable = node->enable;

        return AE_TRUE;
    }

    return AE_FALSE;
}
//////////////////////////////////////////////////////////////////////////
const aeMovieSubComposition * ae_get_movie_sub_composition( const aeMovieComposition * _composition, const ae_char_t * _name )
{
    const aeMovieInstance * instance = _composition->movie_data->instance;

    const aeMovieSubComposition *it_subcomposition = _composition->subcompositions;
    const aeMovieSubComposition *it_subcomposition_end = _composition->subcompositions + _composition->subcomposition_count;
    for( ; it_subcomposition != it_subcomposition_end; ++it_subcomposition )
    {
        const aeMovieSubComposition * subcomposition = it_subcomposition;

        const aeMovieLayerData * layer = subcomposition->layer;

        if( AE_STRNCMP( instance, layer->name, _name, AE_MOVIE_MAX_LAYER_NAME ) != 0 )
        {
            continue;
        }

        return subcomposition;
    }

    return AE_NULL;
}
//////////////////////////////////////////////////////////////////////////
const ae_char_t * ae_get_movie_sub_composition_name( const aeMovieSubComposition * _subcomposition )
{
    const ae_char_t * name = _subcomposition->layer->name;

    return name;
}
//////////////////////////////////////////////////////////////////////////
void ae_get_movie_sub_composition_in_out_loop( const aeMovieSubComposition * _subcomposition, ae_float_t * _in, ae_float_t * _out )
{
    aeMovieCompositionAnimation * animation = _subcomposition->animation;

    ae_float_t work_begin = ae_max_f_f( animation->loop_segment_begin, animation->work_area_begin );
    ae_float_t work_end = ae_min_f_f( animation->loop_segment_end, animation->work_area_end );

    *_in = work_begin;
    *_out = work_end;
}
//////////////////////////////////////////////////////////////////////////
ae_bool_t ae_play_movie_sub_composition( const aeMovieComposition * _composition, const aeMovieSubComposition * _subcomposition, ae_float_t _time )
{
    const aeMovieCompositionData * composition_data = _subcomposition->composition_data;
    aeMovieCompositionAnimation * animation = _subcomposition->animation;

    if( animation->play == AE_TRUE )
    {
        return AE_TRUE;
    }

    if( _time >= 0.f )
    {
        ae_float_t work_time = ae_minimax_f_f( _time, animation->work_area_begin, animation->work_area_end );

        __set_movie_composition_time( _composition, composition_data, animation, work_time, _subcomposition );
    }

    animation->play = AE_TRUE;
    animation->pause = AE_FALSE;
    animation->interrupt = AE_FALSE;

    return AE_TRUE;
}
//////////////////////////////////////////////////////////////////////////
ae_bool_t ae_stop_movie_sub_composition( const aeMovieComposition * _composition, const aeMovieSubComposition * _subcomposition )
{
    aeMovieCompositionAnimation * animation = _subcomposition->animation;

    if( animation->play == AE_FALSE )
    {
        return AE_TRUE;
    }

    __notify_stop_nodies( _composition, _subcomposition->composition_data, animation, _subcomposition );

    ae_set_movie_sub_composition_time( _composition, _subcomposition, animation->work_area_begin );

    animation->play = AE_FALSE;
    animation->pause = AE_FALSE;
    animation->interrupt = AE_FALSE;

    aeMovieCompositionStateCallbackData callbackData;

    callbackData.state = AE_MOVIE_SUB_COMPOSITION_STOP;
    callbackData.subcomposition = _subcomposition;

    (_composition->providers.composition_state)(&callbackData, _composition->provider_data);

    return AE_TRUE;
}
//////////////////////////////////////////////////////////////////////////
ae_bool_t ae_pause_movie_sub_composition( const aeMovieComposition * _composition, const aeMovieSubComposition * _subcomposition )
{
    aeMovieCompositionAnimation * animation = _subcomposition->animation;

    if( animation->play == AE_FALSE )
    {
        return AE_TRUE;
    }

    if( animation->pause == AE_TRUE )
    {
        return AE_TRUE;
    }

    __notify_pause_nodies( _composition, _subcomposition->composition_data, animation, _subcomposition );

    animation->pause = AE_TRUE;

    aeMovieCompositionStateCallbackData callbackData;

    callbackData.state = AE_MOVIE_SUB_COMPOSITION_PAUSE;
    callbackData.subcomposition = _subcomposition;

    (_composition->providers.composition_state)(&callbackData, _composition->provider_data);

    return AE_TRUE;
}
//////////////////////////////////////////////////////////////////////////
ae_bool_t ae_resume_movie_sub_composition( const aeMovieComposition * _composition, const aeMovieSubComposition * _subcomposition )
{
    aeMovieCompositionAnimation * animation = _subcomposition->animation;

    if( animation->play == AE_FALSE )
    {
        return AE_TRUE;
    }

    if( animation->pause == AE_FALSE )
    {
        return AE_TRUE;
    }

    __notify_resume_nodies( _composition, _subcomposition->composition_data, animation, _subcomposition );

    animation->pause = AE_FALSE;

    aeMovieCompositionStateCallbackData callbackData;

    callbackData.state = AE_MOVIE_SUB_COMPOSITION_RESUME;
    callbackData.subcomposition = _subcomposition;

    (_composition->providers.composition_state)(&callbackData, _composition->provider_data);

    return AE_TRUE;
}
//////////////////////////////////////////////////////////////////////////
void ae_interrupt_movie_sub_composition( const aeMovieComposition * _composition, const aeMovieSubComposition * _subcomposition, ae_bool_t _skip )
{
    const aeMovieCompositionData * composition_data = _subcomposition->composition_data;
    aeMovieCompositionAnimation * animation = _subcomposition->animation;

    if( animation->play == AE_FALSE )
    {
        return;
    }

    if( animation->pause == AE_TRUE )
    {
        return;
    }

    if( animation->interrupt == AE_TRUE )
    {
        return;
    }

    animation->interrupt = AE_TRUE;

    if( _skip == AE_TRUE )
    {
        ae_float_t loop_work_end = __get_animation_loop_work_end( animation );

        if( animation->time < loop_work_end )
        {
            __set_movie_composition_time( _composition, composition_data, animation, loop_work_end, _subcomposition );
        }
    }

    aeMovieCompositionStateCallbackData callbackData;

    callbackData.state = AE_MOVIE_SUB_COMPOSITION_INTERRUPT;
    callbackData.subcomposition = _subcomposition;

    (_composition->providers.composition_state)(&callbackData, _composition->provider_data);
}
//////////////////////////////////////////////////////////////////////////
ae_bool_t ae_is_play_movie_sub_composition( const aeMovieSubComposition * _subcomposition )
{
    const aeMovieCompositionAnimation * animation = _subcomposition->animation;

    ae_bool_t play = animation->play;

    return play;
}
//////////////////////////////////////////////////////////////////////////
ae_bool_t ae_is_pause_movie_sub_composition( const aeMovieSubComposition * _subcomposition )
{
    const aeMovieCompositionAnimation * animation = _subcomposition->animation;

    ae_bool_t pause = animation->pause;

    return pause;
}
//////////////////////////////////////////////////////////////////////////
ae_bool_t ae_is_interrupt_movie_sub_composition( const aeMovieSubComposition * _subcomposition )
{
    const aeMovieCompositionAnimation * animation = _subcomposition->animation;

    ae_bool_t interrupt = animation->interrupt;

    return interrupt;
}
//////////////////////////////////////////////////////////////////////////
void ae_set_movie_sub_composition_time( const aeMovieComposition * _composition, const aeMovieSubComposition * _subcomposition, ae_float_t _time )
{
    const aeMovieCompositionData * composition_data = _subcomposition->composition_data;
    aeMovieCompositionAnimation * animation = _subcomposition->animation;

    __set_movie_composition_time( _composition, composition_data, animation, _time, _subcomposition );
}
//////////////////////////////////////////////////////////////////////////
ae_float_t ae_get_movie_sub_composition_time( const aeMovieSubComposition * _subcomposition )
{
    const aeMovieCompositionAnimation * animation = _subcomposition->animation;

    ae_float_t time = animation->time;

    return time;
}
//////////////////////////////////////////////////////////////////////////
void ae_set_movie_sub_composition_loop( const aeMovieSubComposition * _subcomposition, ae_bool_t _loop )
{
    aeMovieCompositionAnimation * animation = _subcomposition->animation;

    animation->loop = _loop;
}
//////////////////////////////////////////////////////////////////////////
ae_bool_t ae_get_movie_sub_composition_loop( const aeMovieSubComposition * _subcomposition )
{
    const aeMovieCompositionAnimation * animation = _subcomposition->animation;

    ae_bool_t loop = animation->loop;

    return loop;
}
//////////////////////////////////////////////////////////////////////////
ae_bool_t ae_set_movie_sub_composition_work_area( const aeMovieComposition * _composition, const aeMovieSubComposition * _subcomposition, ae_float_t _begin, ae_float_t _end )
{
    ae_float_t duration = _composition->composition_data->duration;

    if( _begin < 0.f || _end < 0.f || _begin > duration || _end > duration || _begin > _end )
    {
        return AE_FALSE;
    }

    const aeMovieCompositionData * composition_data = _subcomposition->composition_data;
    aeMovieCompositionAnimation * animation = _subcomposition->animation;

    animation->work_area_begin = _begin;
    animation->work_area_end = _end;

    if( animation->time < _begin || animation->time >= _end )
    {
        __set_movie_composition_time( _composition, composition_data, animation, _begin, _subcomposition );
    }

    return AE_TRUE;
}
//////////////////////////////////////////////////////////////////////////
void ae_remove_movie_sub_composition_work_area( const aeMovieComposition * _composition, const aeMovieSubComposition * _subcomposition )
{
    const aeMovieCompositionData * composition_data = _subcomposition->composition_data;
    aeMovieCompositionAnimation * animation = _subcomposition->animation;

    animation->work_area_begin = 0.f;
    animation->work_area_end = _composition->composition_data->duration;

    __set_movie_composition_time( _composition, composition_data, animation, 0.f, _subcomposition );
}
//////////////////////////////////////////////////////////////////////////