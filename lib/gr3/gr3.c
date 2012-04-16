/*!\file gr3.c
 * ToDo:
 * - gr3_drawimage
 * Bugs:
 * - glXCreatePbuffer -> "failed to create drawable" probably caused by being 
 *      run in virtual box, other applications show the same error message
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h> /* for sqrt */

#include "gr3.h"
#include "gr.h"

#ifndef M_PI
    #define M_PI 3.141592653589793238462643383279
#endif

#if defined(__APPLE__)
    /* Core OpenGL (CGL) on Mac OS X */
    #define GR3_USE_CGL
    #include <OpenGL/OpenGL.h>
    /* OpenGL.h in Mac OS X 10.7 doesn't include gl.h anymore */
    #include <OpenGL/gl.h>
    #include <unistd.h> /* for getpid() for tempfile names */
#elif defined(__linux__)
    /* OpenGL Extension to the X Window System (GLX) on Linux */
    #define GR3_USE_GLX
    #include <GL/gl.h>
    #include <GL/glext.h>
    #include <GL/glx.h>
    #include <unistd.h> /* for getpid() for tempfile names */
#elif defined(_WIN32)
    /* Windows */
    #define GR3_USE_WIN
    #define WIN32_LEAN_AND_MEAN 1
    #include <windows.h>
    #include <GL/gl.h>
    #include "GL/glext.h"
#else
    #error "This operating system is currently not supported by gr3"
#endif

#include <jpeglib.h>
#include <png.h>

#if !(GL_ARB_framebuffer_object || GL_EXT_framebuffer_object)
    #error "Neither GL_ARB_framebuffer_object nor GL_EXT_framebuffer_object \
            are supported!"
#endif

#if GL_VERSION_2_1 
    #define GR3_CAN_USE_VBO
#endif

#if defined(GR3_USE_WIN) || defined(GR3_USE_GLX)
    #ifdef GR3_CAN_USE_VBO
        static PFNGLBUFFERDATAPROC glBufferData;
        static PFNGLBINDBUFFERPROC glBindBuffer;
        static PFNGLGENBUFFERSPROC glGenBuffers;
        static PFNGLGENBUFFERSPROC glDeleteBuffers;
        static PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer;
        static PFNGLGETATTRIBLOCATIONPROC glGetAttribLocation;
        static PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray;
        static PFNGLUSEPROGRAMPROC glUseProgram;
        static PFNGLDELETESHADERPROC glDeleteShader;
        static PFNGLLINKPROGRAMPROC glLinkProgram;
        static PFNGLATTACHSHADERPROC glAttachShader;
        static PFNGLCREATESHADERPROC glCreateShader;
        static PFNGLCOMPILESHADERPROC glCompileShader;
        static PFNGLCREATEPROGRAMPROC glCreateProgram;
        static PFNGLDELETEPROGRAMPROC glDeleteProgram;
        static PFNGLUNIFORM3FPROC glUniform3f;
        static PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv;
        static PFNGLUNIFORM4FPROC glUniform4f;
        static PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation;
        static PFNGLSHADERSOURCEPROC glShaderSource;
    #endif
    static PFNGLDRAWBUFFERSPROC glDrawBuffers;
#if defined(GR3_USE_WIN)
    static PFNGLBLENDCOLORPROC glBlendColor;
#endif
    #ifdef GL_ARB_framebuffer_object
        static PFNGLBINDRENDERBUFFERPROC glBindRenderbuffer;
        static PFNGLCHECKFRAMEBUFFERSTATUSPROC glCheckFramebufferStatus;
        static PFNGLFRAMEBUFFERRENDERBUFFERPROC glFramebufferRenderbuffer;
        static PFNGLRENDERBUFFERSTORAGEPROC glRenderbufferStorage;
        static PFNGLBINDFRAMEBUFFERPROC glBindFramebuffer;
        static PFNGLGENFRAMEBUFFERSPROC glGenFramebuffers;
        static PFNGLGENRENDERBUFFERSPROC glGenRenderbuffers;
        static PFNGLDELETEFRAMEBUFFERSPROC glDeleteFramebuffers;
        static PFNGLDELETERENDERBUFFERSPROC glDeleteRenderbuffers;
    #endif
    #ifdef GL_EXT_framebuffer_object
        static PFNGLBINDRENDERBUFFEREXTPROC glBindRenderbufferEXT;
        static PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC glCheckFramebufferStatusEXT;
        static PFNGLFRAMEBUFFERRENDERBUFFEREXTPROC glFramebufferRenderbufferEXT;
        static PFNGLRENDERBUFFERSTORAGEEXTPROC glRenderbufferStorageEXT;
        static PFNGLBINDFRAMEBUFFEREXTPROC glBindFramebufferEXT;
        static PFNGLGENFRAMEBUFFERSEXTPROC glGenFramebuffersEXT;
        static PFNGLGENRENDERBUFFERSEXTPROC glGenRenderbuffersEXT;
        static PFNGLDELETEFRAMEBUFFERSEXTPROC glDeleteFramebuffersEXT;
        static PFNGLDELETERENDERBUFFERSEXTPROC glDeleteRenderbuffersEXT;
    #endif
#endif

/*!
 * An instance of this struct contains all information from the 
 * ::int list given to gr3_init() or the default values from 
 * ::GR3_InitStruct_INITIALIZER.
 * \note Except for when gr3_init() is run, the only existing instance of this 
 * struct should be the one in #context_struct_.
 */
typedef struct _GR3_InitStruct_t_ {
    int framebuffer_width; /*!< The width of the framebuffer used for 
                                generating images */
    int framebuffer_height; /*!< The height of the framebuffer used for 
                                 generating images */
} GR3_InitStruct_t_;
/*!
 * The default values for the instances of GR3_InitStruct_t_:\n
 * GR3_InitStruct_t_::framebuffer_width = 512;\n
 * GR3_InitStruct_t_::framebuffer_height = 512;
 */
#define GR3_InitStruct_INITIALIZER {512,512}

/*!
 * Each call to gr3_drawmesh() gets saved in an instance of this struct, so it
 * can be used for later drawing. They form a linked-list, with each draw call
 * pointing to the next one.
 */
typedef struct _GR3_DrawList_t_ {
    int mesh; /*!< The id of the mesh that should be drawn */
    float *positions; /*!< A list of 3 * _GR3_DrawList_t_::n floats. 
                           Each triple is one position 
                           for one mesh to be drawn. */
    float *directions; /*!< A list of 3 * _GR3_DrawList_t_::n floats. 
                            Each triple is one (forward) direction 
                            for one mesh to be drawn. */
    float *ups; /*!< A list of 3 * _GR3_DrawList_t_::n floats. 
                     Each triple is one up vector 
                     for one mesh to be drawn. */
    float *colors; /*!< A list of 3 * _GR3_DrawList_t_::n floats. 
                        Each triple is one color 
                        for one mesh to be drawn. */
    float *scales; /*!< A list of 3 * _GR3_DrawList_t_::n floats. 
                        Each triple means the scaling factors 
                        for one mesh to be drawn. */
    int n;          /*!< The number of meshes to be drawn. */
    struct _GR3_DrawList_t_ *next; /*!< The pointer to the next GR3_DrawList_t_. */
} GR3_DrawList_t_;

typedef enum _GR3_MeshType_t {
    kMTNormalMesh,
    kMTConeMesh,
    kMTSphereMesh,
    kMTCylinderMesh
} GR3_MeshType_t;

/*!
 * This union contains all information required for using a mesh. Either the 
 * display list id, or the vertex buffer object id and number of vertices. 
 * Instances of this unions are kept and 
 * reference counted in the ::GR3_MeshList_t_.
 */
typedef struct _GR3_MeshData_t_ {
    GR3_MeshType_t type;
    union  {
        int display_list_id; /*!< The OpenGL display list of the mesh. */
        unsigned int vertex_buffer_id;
    } data;
    float * vertices;
    float * normals;
    float * colors;
    int number_of_vertices;
} GR3_MeshData_t_;

/*!
 * This struct is for managing ::GR3_MeshData_t_ objects.
 * Instances of this array are meant to be kept in an array so they can be 
 * accessed by an index. Each instance refers to the next free instance in the 
 * list, so when the user needs a new one, a free one can be found just by 
 * indexing the array. They are reference counted and are 'free' if there are 
 * no references left and _GR3_MeshList_t_::refcount reaches zero. A user can 
 * only get one reference to the mesh by calling gr3_createmesh() and therefore 
 * should only be able to release one by calling gr3_deletemesh(). This is 
 * realized by saving a flag (whether a mesh is marked for deletion or not) 
 * in _GR3_MeshList_t_::marked_for_deletion.
 * \note Instances of this struct should only appear in the #context_struct_.
 * \note Reference counting is handled by calling gr3_meshaddreference_() and 
 * gr3_meshremovereference_().
 */
typedef struct _GR3_MeshList_t_ {
    GR3_MeshData_t_ data; /*!<  The data of the actual mesh managed by this 
                                struct. */
    int refcount; /*!< A reference counter. If refcount reaches zero, 
                       the instance is unused or 'free' and can be used
                       for a different mesh. */
    int marked_for_deletion; /*!< A flag whether the user called 
                                  gr3_deletemesh() for this mesh. */ 
    int next_free; /*!< If refcount is zero (so this object is free), 
                        next_free is the index of the next free object 
                        in the array it is stored in. */
} GR3_MeshList_t_;

/*!
 * This function pointer holds the function used by gr3_log_(). It can be set 
 * with gr3_setlogcallback().
 */
static void (*gr3_log_func_)(const char *log_message) = NULL;

/*!
 * This char array is returned by gr3_getrenderpathstring() if it is called 
 * without calling gr3_init() successfully first.
 */
static char not_initialized_[] = "Not initialized";

/*!
 * The id of the framebuffer object used for rendering.
 */
static GLuint framebuffer = 0;

/*!
 * This struct holds all context data. All data that is dependent on gr3 to 
 * be initialized is saved here. It is set up by gr3_init() and turned back 
 * into its default state by gr3_terminate().
 * \note #context_struct_ should be the only instance of this struct.
 */
typedef struct _GR3_ContextStruct_t_ {
    GR3_InitStruct_t_ init_struct; /*!< The information given to gr3_init().
                                        \note This member and its members must not 
                                        be changed outside of gr3_init() or 
                                        gr3_terminate(). 
                                        \sa _GR3_InitStruct_t_ */

    int is_initialized;          /*!< This flag is set to 1 if gr3 was 
                                      initialized successfully. */
    
    int gl_is_initialized;       /*!< This flag is set to 1 if an OpenGL context 
                                      has been created successfully. */

    void(*terminateGL)(void);    /*!< This member holds a pointer to the 
                                      function which must be used for destroying 
                                      the OpenGL context.
                                      \sa gr3_terminateGL_WIN_()
                                      \sa gr3_terminateGL_GLX_()
                                      \sa gr3_terminateGL_CGL_()
                                    */
    
    int fbo_is_initialized;      /*!< This flag is set to 1 if a framebuffer 
                                      object has been created successfully. */

    void(*terminateFBO)(void);   /*!< This member holds a pointer to the 
                                      function which must be used for destroying 
                                      the framebuffer object.
                                      \sa gr3_terminateFBO_ARB_()
                                      \sa gr3_terminateFBO_EXT_()
                                    */

    char *renderpath_string;     /*!< This string holds information on the gr3 
                                      renderpath.
                                      \sa gr3_getrenderpathstring()
                                      \sa gr3_appendtorenderpathstring_()
                                    */

    GR3_DrawList_t_ *draw_list_;   /*!< This member holds a pointer to the first 
                                        element of the linked draw list or NULL. */
    
    GR3_MeshList_t_ *mesh_list_;   /*!< This member holds a pointer to the mesh 
                                        list or NULL. */

    int mesh_list_first_free_; /*!< The index of the first free element 
                                    in the mesh list*/
    int mesh_list_capacity_; /*!< The number of elements in the mesh 
                                  list */

    GLfloat view_matrix[4][4]; /*!< The view matrix used to transform vertices 
                                    from world to eye space. */
    float vertical_field_of_view; /*!< The vertical field of view, used for the
                                       projection marix */
    float zNear; /*!< distance to the near clipping plane */
    float zFar;  /*!< distance to the far clipping plane */

    GLfloat light_dir[4]; /*!< The direction of light + 0 for showing that it is 
                               not a position, but a direction */
    int use_vbo;

    int cylinder_mesh;
    int sphere_mesh;
    int cone_mesh;
    GLfloat background_color[4];
    GLuint program;
    float camera_x; float camera_y; float camera_z;
    float center_x; float center_y; float center_z; 
    float up_x;     float up_y;     float up_z;
    GLfloat *projection_matrix;
    int quality;
} GR3_ContextStruct_t_;
/*!
 * The only instance of ::GR3_ContextStruct_t_. For documentation, see 
 * ::_GR3_ContextStruct_t_.
 */
#define GR3_ContextStruct_INITIALIZER {GR3_InitStruct_INITIALIZER,0,0,\
                                                NULL,0,NULL,not_initialized_,\
                                                NULL, NULL,0,0,{{0}},0,0,0,\
                                                {0,0,0,0},0,0,0,0,{0,0,0,1},0,\
                                                0,0,0,0,0,0,0,0,0, NULL,0}
static GR3_ContextStruct_t_ context_struct_ = GR3_ContextStruct_INITIALIZER;

/* For documentation, see the definition. */
static int       gr3_extensionsupported_(const char *extension_name);
static void      gr3_appendtorenderpathstring_(const char *string);
static void      gr3_log_(const char *log_message);
static int       gr3_export_html_(const char *filename, int width, int height);
static int       gr3_export_png_(const char *filename, int width, int height);
static int       gr3_export_jpeg_(const char *filename, int width, int height);
static int       gr3_export_pov_(const char *filename, int width, int height);
static void      gr3_meshaddreference_(int mesh);
static void      gr3_meshremovereference_(int mesh);
static void      gr3_dodrawmesh_(int mesh, 
                                int n, const float *positions, 
                                const float *directions, const float *ups, 
                                const float *colors, const float *scales);
static void     gr3_createcylindermesh_(void);
static void     gr3_createspheremesh_(void);
static void     gr3_createconemesh_(void);
static int      gr3_readpngtomemory_(int *pixels, const char *pngfile, int width, int height);
static int      gr3_getpixmap_(char *bitmap, int width, int height, int use_alpha, int ssaa_factor);
static int      gr3_getpovray_(char *bitmap, int width, int height, int use_alpha, int ssaa_factor);
#ifdef GR3_USE_CGL
    static int gr3_initGL_CGL_(void);
    static void      gr3_terminateGL_CGL_(void);
#endif
#ifdef GR3_USE_GLX
    static int gr3_initGL_GLX_(void);
    static void      gr3_terminateGL_GLX_Pbuffer_(void);
    static void      gr3_terminateGL_GLX_Pixmap_(void);
#endif
#ifdef GR3_USE_WIN
    static int gr3_initGL_WIN_(void);
    static void      gr3_terminateGL_WIN_(void);
#endif

#if GL_EXT_framebuffer_object
static int gr3_initFBO_EXT_(void);
static void      gr3_terminateFBO_EXT_(void);
#endif
#if GL_ARB_framebuffer_object
static int gr3_initFBO_ARB_(void);
static void      gr3_terminateFBO_ARB_(void);
#endif


/*!
 * This method initializes the gr3 context.
 *
 * \param [in] attrib_list  This ::GR3_IA_END_OF_LIST-terminated list can 
 *                          specify details about context creation. The 
 *                          attributes which use unsigned integer values are 
 *                          followed by these values.
 * \return 
 * - ::GR3_ERROR_NONE              on success
 * - ::GR3_ERROR_INVALID_VALUE     if one of the attributes' values is out of the 
 *                                 allowed range
 * - ::GR3_ERROR_INVALID_ATTRIBUTE if the list contained an unkown attribute or two 
 *                                 mutually exclusive attributes were both used
 * - ::GR3_ERROR_OPENGL_ERR        if an OpenGL error occured
 * - ::GR3_ERROR_INIT_FAILED       if an error occured during initialization of the 
 *                                 "window toolkit" (CGL, GLX, WIN...)
 * \sa gr3_terminate()
 * \sa context_struct_
 * \sa int
 */
GR3API int gr3_init(int *attrib_list) {
    int i;
    char *renderpath_string = "gr3";
    int error;
    GR3_InitStruct_t_ init_struct = GR3_InitStruct_INITIALIZER;
    if (attrib_list) {
        for (i = 0; attrib_list[i] != GR3_IA_END_OF_LIST; i++) {
            switch (attrib_list[i]) {
                case GR3_IA_FRAMEBUFFER_WIDTH:
                    init_struct.framebuffer_width = attrib_list[++i];
                    if (attrib_list[i] <= 0) 
                        return GR3_ERROR_INVALID_VALUE;
                    break;
                case GR3_IA_FRAMEBUFFER_HEIGHT:
                    init_struct.framebuffer_height = attrib_list[++i];
                    if (attrib_list[i] <= 0) 
                        return GR3_ERROR_INVALID_VALUE;
                    break;
                default:
                    return GR3_ERROR_INVALID_ATTRIBUTE;
            }
        }
    }
    context_struct_.init_struct = init_struct;

    context_struct_.renderpath_string = malloc(strlen(renderpath_string)+1);
    strcpy(context_struct_.renderpath_string, renderpath_string);

    do {
	error = GR3_ERROR_INIT_FAILED;
        #if defined(GR3_USE_CGL)
            error = gr3_initGL_CGL_();
            if (error == GR3_ERROR_NONE) {
                break;
            }
        #endif
        #if defined(GR3_USE_GLX)
            error = gr3_initGL_GLX_();
            if (error == GR3_ERROR_NONE) {
                break;
            }
        #endif
        #if defined(GR3_USE_WIN)
            error = gr3_initGL_WIN_();
            if (error == GR3_ERROR_NONE) {
                break;
            }
        #endif
        gr3_terminate();
        return error;
    } while (0);
    
    /* GL_ARB_framebuffer_object is core since OpenGL 3.0 */
    #if GL_ARB_framebuffer_object
    if (!strncmp((const char *)glGetString(GL_VERSION),"3.",2) || !strncmp((const char *)glGetString(GL_VERSION),"4.",2) || 
            gr3_extensionsupported_("GL_ARB_framebuffer_object")
       ) {
        error = gr3_initFBO_ARB_();
        if (error) {
            gr3_terminate();
            return error;
        }
    } else 
    #endif
    #if GL_EXT_framebuffer_object
    if (gr3_extensionsupported_("GL_EXT_framebuffer_object")) {
        error = gr3_initFBO_EXT_();
        if (error) {
            gr3_terminate();
            return error;
        }
    } else 
    #endif 
    {
        gr3_terminate();
        return GR3_ERROR_OPENGL_ERR;
    }

    #ifdef GR3_CAN_USE_VBO
        if (strncmp((const char *)glGetString(GL_VERSION),"2.1",3)>=0) {
            context_struct_.use_vbo = 1;
        }
        if (context_struct_.use_vbo) {
            GLuint program;
            GLuint fragment_shader;
            GLuint vertex_shader;
            char *vertex_shader_source[] = {"#version 120\n",
            "uniform mat4 ProjectionMatrix;\n",
            "uniform mat4 ViewMatrix;\n",
            "uniform mat4 ModelMatrix;\n",
            "uniform vec3 LightDirection;\n",
            "uniform vec4 Scales;\n",

            "attribute vec3 in_Vertex;\n"
            "attribute vec3 in_Normal;\n"
            "attribute vec3 in_Color;\n"
            
            "varying vec4 Color;\n",
            "varying vec3 Normal;\n",
            
            "void main(void) {\n",
                "vec4 Position = ViewMatrix*ModelMatrix*(Scales*vec4(in_Vertex,1));\n",
                "gl_Position=ProjectionMatrix*Position;\n",
                "Normal = mat3(ViewMatrix)*mat3(ModelMatrix)*in_Normal;\n",
                "Color = vec4(in_Color,1);\n",
                "float diffuse = Normal.z;\n",
                "if (dot(LightDirection,LightDirection) > 0.001) {",
                "diffuse = dot(normalize(LightDirection),Normal);",
                "}",
                "diffuse = abs(diffuse);\n",
                "Color.rgb = diffuse*Color.rgb;"
            "}\n"};
            
            char *fragment_shader_source[] = {"#version 120\n",
            "varying vec4 Color;\n",
            "varying vec3 Normal;\n",
            "uniform mat4 ViewMatrix;\n",
            
            "void main(void) {\n",
            "gl_FragColor=vec4(Color.rgb,Color.a);\n",
            "}\n"};
            program = glCreateProgram();
            vertex_shader = glCreateShader(GL_VERTEX_SHADER);
            glShaderSource(vertex_shader, sizeof(vertex_shader_source)/sizeof(char *), (const GLchar **)vertex_shader_source, NULL);
            glCompileShader(vertex_shader);
            fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
            glShaderSource(fragment_shader, sizeof(fragment_shader_source)/sizeof(char *), (const GLchar **)fragment_shader_source, NULL);
            glCompileShader(fragment_shader);
            glAttachShader(program, vertex_shader);
            glAttachShader(program, fragment_shader);
            glLinkProgram(program);
            glDeleteShader(vertex_shader);
            glDeleteShader(fragment_shader);
            context_struct_.program = program;
            glUseProgram(program);
            
            gr3_appendtorenderpathstring_("Vertex Buffer Objects");
        } else
    #endif
    {
        gr3_appendtorenderpathstring_("Display Lists");
    }


    context_struct_.is_initialized = 1;
    
    gr3_appendtorenderpathstring_((const char *)glGetString(GL_VERSION));
    gr3_appendtorenderpathstring_((const char *)glGetString(GL_RENDERER));
    gr3_createcylindermesh_();
    gr3_createspheremesh_();
    gr3_createconemesh_();
    return GR3_ERROR_NONE;
}

/*!
 * This helper function checks whether an OpenGL extension is supported.
 * \param [in] extension_name The extension that should be checked for, 
 *             e.g. GL_EXT_framebuffer_object. The notation has to
 *             be the same as in the GL_EXTENSIONS string.
 * \returns 1 on success, 0 otherwise.
 */
static int gr3_extensionsupported_(const char *extension_name) {
    const char *extension_string = (const char *)glGetString(GL_EXTENSIONS);
    return strstr(extension_string, extension_name) != NULL;
}

/*!
 * This function terminates the gr3 context. 
 * After calling this function, gr3 is in the same state as when it was first 
 * loaded, except for context-independent variables, i.e. the logging callback.
 * \note It is safe to call this function even if the context is not 
 * initialized.
 * \sa gr3_init()
 * \sa context_struct_
 */
GR3API void gr3_terminate(void) {
    if (context_struct_.gl_is_initialized) {
#ifdef GR3_CAN_USE_VBO
        if (context_struct_.use_vbo) {
            glUseProgram(0);
            glDeleteProgram(context_struct_.program);
        }
#endif
        gr3_deletemesh(context_struct_.cylinder_mesh);
        gr3_deletemesh(context_struct_.sphere_mesh);
        gr3_deletemesh(context_struct_.cone_mesh);
        if (context_struct_.fbo_is_initialized) {
            int i;
            gr3_clear();
            for (i = 0; i < context_struct_.mesh_list_capacity_; i++) {
                if (context_struct_.mesh_list_[i].data.data.display_list_id != 0) {
                    glDeleteLists(
                        context_struct_.mesh_list_[i].data.data.display_list_id,1);
                    context_struct_.mesh_list_[i].data.data.display_list_id = 0;
                    free(context_struct_.mesh_list_[i].data.vertices);
                    free(context_struct_.mesh_list_[i].data.normals);
                    free(context_struct_.mesh_list_[i].data.colors);
                    context_struct_.mesh_list_[i].refcount = 0;
                    context_struct_.mesh_list_[i].marked_for_deletion = 0;
                }
            }
            
            free(context_struct_.mesh_list_);
            context_struct_.mesh_list_ = NULL;
            context_struct_.mesh_list_capacity_ = 0;
            context_struct_.mesh_list_first_free_ = 0;
            
            context_struct_.terminateFBO();
        }
        context_struct_.terminateGL();
    }
    context_struct_.is_initialized = 0;
    if (context_struct_.renderpath_string != not_initialized_) {
        free(context_struct_.renderpath_string);
        context_struct_.renderpath_string = not_initialized_;
    }
    {
        GR3_ContextStruct_t_ initializer = GR3_ContextStruct_INITIALIZER;
        context_struct_ = initializer;
    }
}

/*!
 * This function clears the draw list.
 * \returns
 * - ::GR3_ERROR_NONE             on success
 * - ::GR3_ERROR_OPENGL_ERR       if an OpenGL error occured
 * - ::GR3_ERROR_NOT_INITIALIZED  if the function was called without 
 *                                calling gr3_init() first
 */
GR3API int gr3_clear(void) {
    gr3_log_("gr3_clear();");
    
    if (context_struct_.is_initialized) {
        GR3_DrawList_t_ *draw;
        while (context_struct_.draw_list_) {
            draw = context_struct_.draw_list_;
            context_struct_.draw_list_ = draw->next;
            gr3_meshremovereference_(draw->mesh);
            free(draw->positions);
            free(draw->directions);
            free(draw->ups);
            free(draw->colors);
            free(draw->scales);
            free(draw);
        }
        
        if (glGetError() == GL_NO_ERROR) {
            return GR3_ERROR_NONE;
        } else {
            return GR3_ERROR_OPENGL_ERR;
        }
    } else {
        return GR3_ERROR_NOT_INITIALIZED;
    }
}

/*!
 * This function sets the background color.
 */
GR3API void gr3_setbackgroundcolor(float red, float green, float blue, float alpha) {
    if (context_struct_.is_initialized) {
        context_struct_.background_color[0] = red;
        context_struct_.background_color[1] = green;
        context_struct_.background_color[2] = blue;
        context_struct_.background_color[3] = alpha;
    }
}

/*!
 * This function creates a int from vertex position, normal and color data.
 * \param [out] mesh          a pointer to a int 
 * \param [in] vertices       the vertex positions
 * \param [in] normals        the vertex normals
 * \param [in] colors         the vertex colors, they should be white (1,1,1) if 
 *                            you want to change the color for each drawn mesh
 * \param [in] n              the number of vertices in the mesh
 *
 * \returns
 *  - ::GR3_ERROR_NONE        on success
 *  - ::GR3_ERROR_OPENGL_ERR  if an OpenGL error occured
 *  - ::GR3_ERROR_OUT_OF_MEM  if a memory allocation failed
 */
GR3API int gr3_createmesh(int *mesh, int n, const float *vertices, 
                        const float *normals, const float *colors) {
                        
    int i;
    void *mem;

    if (!context_struct_.is_initialized) {
        return GR3_ERROR_NOT_INITIALIZED;
    }

    *mesh = context_struct_.mesh_list_first_free_;
    if (context_struct_.mesh_list_first_free_ >= context_struct_.mesh_list_capacity_) {
        int new_capacity = context_struct_.mesh_list_capacity_*2;
        if (context_struct_.mesh_list_capacity_ == 0) {
            new_capacity = 8;
        }
        mem = realloc(context_struct_.mesh_list_, new_capacity*sizeof(*context_struct_.mesh_list_));
        if (mem == NULL) {
            return GR3_ERROR_OUT_OF_MEM;
        }
        context_struct_.mesh_list_ = mem;
        while(context_struct_.mesh_list_capacity_ < new_capacity) {
            context_struct_.mesh_list_[context_struct_.mesh_list_capacity_].next_free = context_struct_.mesh_list_capacity_+1;
            context_struct_.mesh_list_[context_struct_.mesh_list_capacity_].refcount = 0;
            context_struct_.mesh_list_[context_struct_.mesh_list_capacity_].marked_for_deletion = 0;
            context_struct_.mesh_list_[context_struct_.mesh_list_capacity_].data.type = kMTNormalMesh;
            context_struct_.mesh_list_[context_struct_.mesh_list_capacity_].data.data.display_list_id = 0;
            context_struct_.mesh_list_[context_struct_.mesh_list_capacity_].data.data.vertex_buffer_id = 0;
            context_struct_.mesh_list_[context_struct_.mesh_list_capacity_].data.number_of_vertices = 0;
            context_struct_.mesh_list_capacity_++;
        }
    }
    context_struct_.mesh_list_first_free_ = context_struct_.mesh_list_[*mesh].next_free;
    
    gr3_meshaddreference_(*mesh);
    #ifdef GR3_CAN_USE_VBO
    if (context_struct_.use_vbo) {
        glGenBuffers(1, &context_struct_.mesh_list_[*mesh].data.data.vertex_buffer_id);
        context_struct_.mesh_list_[*mesh].data.number_of_vertices = n;
        glBindBuffer(GL_ARRAY_BUFFER, context_struct_.mesh_list_[*mesh].data.data.vertex_buffer_id);
        mem = malloc(n*3*3*sizeof(GLfloat));
        if (mem == NULL) {
            return GR3_ERROR_OUT_OF_MEM;
        }
        for (i = 0; i < n; i++) {
            GLfloat *data = ((GLfloat *)mem)+i*3*3;
            data[0] = vertices[i*3+0];
            data[1] = vertices[i*3+1];
            data[2] = vertices[i*3+2];
            data[3] = normals[i*3+0];
            data[4] = normals[i*3+1];
            data[5] = normals[i*3+2];
            data[6] = colors[i*3+0];
            data[7] = colors[i*3+1];
            data[8] = colors[i*3+2];
        }
        glBufferData(GL_ARRAY_BUFFER, n*3*3*sizeof(GLfloat), mem, GL_STATIC_DRAW);
        free(mem);
        glBindBuffer(GL_ARRAY_BUFFER,0);
    } else
    #endif
    {
        context_struct_.mesh_list_[*mesh].data.data.display_list_id = glGenLists(1);
        glNewList(context_struct_.mesh_list_[*mesh].data.data.display_list_id, GL_COMPILE);
        glBegin(GL_TRIANGLES);
        for (i = 0; i < n; i++) {
            glColor3fv(colors+i*3);
            glNormal3fv(normals+i*3);
            glVertex3fv(vertices+i*3);    
        }
        glEnd();
        glEndList();
    }
    context_struct_.mesh_list_[*mesh].data.vertices = (float *)malloc(sizeof(float)*n*3);
    context_struct_.mesh_list_[*mesh].data.normals = (float *)malloc(sizeof(float)*n*3);
    context_struct_.mesh_list_[*mesh].data.colors = (float *)malloc(sizeof(float)*n*3);
    memcpy(context_struct_.mesh_list_[*mesh].data.vertices,vertices,sizeof(float)*n*3);
    memcpy(context_struct_.mesh_list_[*mesh].data.normals,normals,sizeof(float)*n*3);
    memcpy(context_struct_.mesh_list_[*mesh].data.colors,colors,sizeof(float)*n*3);
    if (glGetError() != GL_NO_ERROR)
        return GR3_ERROR_OPENGL_ERR;
    else
        return GR3_ERROR_NONE;
}

/*!
 * This function adds a mesh to the draw list, so it will be drawn when the user
 * calls gr3_getpixmap_(). The given data stays owned by the user, a copy will be 
 * saved in the draw list and the mesh reference counter will be increased.
 * \param [in] mesh         The mesh to be drawn
 * \param [in] positions    The positions where the meshes should be drawn
 * \param [in] directions   The forward directions the meshes should be facing
 *                          at
 * \param [in] ups          The up directions
 * \param [in] colors       The colors the meshes should be drawn in, it will be 
 *                          multiplied with each vertex color
 * \param [in] scales       The scaling factors
 * \param [in] n            The number of meshes to be drawn
 * \note This function does not return an error code, because of its 
 * asynchronous nature. If gr3_getpixmap_() returns a ::GR3_ERROR_OPENGL_ERR, this 
 * might be caused by this function saving unuseable data into the draw list.
 */
GR3API void gr3_drawmesh(int mesh, int n, const float *positions, 
                  const float *directions, const float *ups, 
                  const float *colors, const float *scales) {

    if (!context_struct_.is_initialized) {
        return;
    } else {
        GR3_DrawList_t_ *p;
        
        GR3_DrawList_t_ *draw = malloc(sizeof(GR3_DrawList_t_));
        draw->mesh = mesh;
        draw->positions = malloc(sizeof(float)*n*3);
        memcpy(draw->positions,positions,sizeof(float)*n*3);
        draw->directions = malloc(sizeof(float)*n*3);
        memcpy(draw->directions,directions,sizeof(float)*n*3);
        draw->ups = malloc(sizeof(float)*n*3);
        memcpy(draw->ups,ups,sizeof(float)*n*3);
        draw->colors = malloc(sizeof(float)*n*3);
        memcpy(draw->colors,colors,sizeof(float)*n*3);
        draw->scales = malloc(sizeof(float)*n*3);
        memcpy(draw->scales,scales,sizeof(float)*n*3);
        draw->n = n;
        draw->next= NULL;
        gr3_meshaddreference_(mesh);
        if (context_struct_.draw_list_ == NULL) {
            context_struct_.draw_list_ = draw;
        } else {
            p = context_struct_.draw_list_;
            while(p->next) {
                p = p->next;
            }
            p->next = draw;
        }
    }
}

/*!
 * This function does the actual mesh drawing. It will be called with the data 
 * in the draw list.
 * \param [in] mesh         The mesh to be drawn
 * \param [in] positions    The positions where the meshes should be drawn
 * \param [in] directions   The forward directions the meshes should be facing
 *                          at
 * \param [in] ups          The up directions
 * \param [in] colors       The colors the meshes should be drawn in, it will be 
 *                          multiplied with each vertex color
 * \param [in] scales       The scaling factors
 * \param [in] n            The number of meshes to be drawn 
 * \returns 
 *  - ::GR3_ERROR_NONE on success
 *  - ::GR3_ERROR_OPENGL_ERR if an OpenGL error occured
 */
static void gr3_dodrawmesh_(int mesh, 
                            int n, const float *positions, 
                            const float *directions, const float *ups, 
                            const float *colors, const float *scales) {
    int i,j;
    GLfloat forward[3], up[3], left[3];
    GLfloat model_matrix[4][4] = {{0}};
    float tmp;
    for (i = 0; i < n; i++) {
        {

            /* Calculate an orthonormal base in IR^3, correcting the up vector 
             * in case it is not perpendicular to the forward vector. This base
             * is used to create the model matrix as a base-transformation 
             * matrix.
             */
            /* forward = normalize(&directions[i*3]); */
            tmp = 0;
            for (j = 0; j < 3; j++) {
                tmp+= directions[i*3+j]*directions[i*3+j];
            }
            tmp = sqrt(tmp);
            for (j = 0; j < 3; j++) {
                forward[j] = directions[i*3+j]/tmp;
            }/* up = normalize(&ups[i*3]); */
            tmp = 0;
            for (j = 0; j < 3; j++) {
                tmp+= ups[i*3+j]*ups[i*3+j];
            }
            tmp = sqrt(tmp);
            for (j = 0; j < 3; j++) {
                up[j] = ups[i*3+j]/tmp;
            }
            /* left = cross(forward,up); */
            for (j = 0; j < 3; j++) {
                left[j] = forward[(j+1)%3]*up[(j+2)%3] - up[(j+1)%3]*forward[(j+2)%3];
            }
            /* up = cross(left,forward); */
            for (j = 0; j < 3; j++) {
                up[j] = left[(j+1)%3]*forward[(j+2)%3] - forward[(j+1)%3]*left[(j+2)%3];
            }
            if (!context_struct_.use_vbo) {
                for (j = 0; j < 3; j++) {
                    model_matrix[0][j] = -left[j]*scales[i*3+0];
                    model_matrix[1][j] = up[j]*scales[i*3+1];
                    model_matrix[2][j] = forward[j]*scales[i*3+2];
                    model_matrix[3][j] = positions[i*3+j];
                }
            } else {
                for (j = 0; j < 3; j++) {
                    model_matrix[0][j] = -left[j];
                    model_matrix[1][j] = up[j];
                    model_matrix[2][j] = forward[j];
                    model_matrix[3][j] = positions[i*3+j];
                }
            }
            model_matrix[3][3] = 1;
        }
        glColorMaterial(GL_FRONT_AND_BACK, GL_DIFFUSE);
        glEnable(GL_COLOR_MATERIAL);
        {
            float nil[4] = {0,0,0,1};
            float one[4] = {1,1,1,1};
            glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT,nil);
            glLightfv(GL_LIGHT0,GL_AMBIENT,nil);
            glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE,one);
            glLightfv(GL_LIGHT0,GL_DIFFUSE,one);
        }
        glBlendColor(colors[i*3+0], colors[i*3+1], colors[i*3+2], 1);
        glBlendFunc(GL_CONSTANT_COLOR, GL_ZERO);
        glEnable(GL_BLEND);
        #ifdef GR3_CAN_USE_VBO
        if (context_struct_.use_vbo) {
            glUniform4f(glGetUniformLocation(context_struct_.program, "Scales"),scales[3*i+0],scales[3*i+1],scales[3*i+2],1);
            glUniformMatrix4fv(glGetUniformLocation(context_struct_.program, "ModelMatrix"), 1,GL_FALSE,&model_matrix[0][0]);
            glBindBuffer(GL_ARRAY_BUFFER, context_struct_.mesh_list_[mesh].data.data.vertex_buffer_id);
            glVertexAttribPointer(glGetAttribLocation(context_struct_.program, "in_Vertex"), 3,GL_FLOAT, GL_FALSE, sizeof(GLfloat)*3*3,(void *)(sizeof(GLfloat)*3*0));
            glVertexAttribPointer(glGetAttribLocation(context_struct_.program, "in_Normal"), 3,GL_FLOAT, GL_FALSE, sizeof(GLfloat)*3*3,(void *)(sizeof(GLfloat)*3*1));
            glVertexAttribPointer(glGetAttribLocation(context_struct_.program, "in_Color"), 3,GL_FLOAT, GL_FALSE, sizeof(GLfloat)*3*3,(void *)(sizeof(GLfloat)*3*2));
            glEnableVertexAttribArray(glGetAttribLocation(context_struct_.program, "in_Vertex"));
            glEnableVertexAttribArray(glGetAttribLocation(context_struct_.program, "in_Normal"));
            glEnableVertexAttribArray(glGetAttribLocation(context_struct_.program, "in_Color"));
            glDrawArrays(GL_TRIANGLES, 0, context_struct_.mesh_list_[mesh].data.number_of_vertices);
        } else
        #endif
        {
            glPushMatrix();
            glMultMatrixf(&model_matrix[0][0]);
            glCallList(context_struct_.mesh_list_[mesh].data.data.display_list_id);
            glPopMatrix();
        }
        glDisable(GL_BLEND);
    }
}

/*!
 * This function marks a mesh for deletion and removes the user's reference 
 * from the mesh's referenc counter, so a user must not use the mesh after 
 * calling this function. If the mesh is still in use for draw calls, the mesh 
 * will not be truly deleted until gr3_clear() is called.
 * \param [in] mesh     The mesh that should be marked for deletion
 */
GR3API void gr3_deletemesh(int mesh) {
    gr3_log_("gr3_deletemesh_();");
    if (!context_struct_.is_initialized) {
        return;
    }
    if (!context_struct_.mesh_list_[mesh].marked_for_deletion) {
        gr3_meshremovereference_(mesh);
        context_struct_.mesh_list_[mesh].marked_for_deletion = 1;
    } else {
        gr3_log_("Mesh already marked for deletion!");
    }
}

/*!
 * This function adds a reference to the meshes reference counter.
 * \param [in] mesh     The mesh to which a reference will be added
 */
static void gr3_meshaddreference_(int mesh) {
    context_struct_.mesh_list_[mesh].refcount++;
}

/*!
 * This function removes a reference from the meshes reference counter and 
 * deletes the mesh if the reference counter reaches zero.
 * \param [in] mesh     The mesh from which a reference will be removed
 */
static void gr3_meshremovereference_(int mesh) {
    if (context_struct_.mesh_list_[mesh].refcount > 0) {
        context_struct_.mesh_list_[mesh].refcount--;
    }
    if (context_struct_.mesh_list_[mesh].refcount <= 0) {
        #ifdef GR3_CAN_USE_VBO
        if (context_struct_.use_vbo) {
            glDeleteBuffers(1,&context_struct_.mesh_list_[mesh].data.data.vertex_buffer_id);
        } else 
        #endif
        {
            glDeleteLists(context_struct_.mesh_list_[mesh].data.data.display_list_id,1);
        }
        free(context_struct_.mesh_list_[mesh].data.vertices);
        free(context_struct_.mesh_list_[mesh].data.normals);
        free(context_struct_.mesh_list_[mesh].data.colors);
        context_struct_.mesh_list_[mesh].data.data.display_list_id = 0;
        context_struct_.mesh_list_[mesh].refcount = 0;
        context_struct_.mesh_list_[mesh].marked_for_deletion = 0;
        if (context_struct_.mesh_list_first_free_ > mesh) {
            context_struct_.mesh_list_[mesh].next_free = context_struct_.mesh_list_first_free_;
            context_struct_.mesh_list_first_free_ = mesh;
        } else {
            int lastf = context_struct_.mesh_list_first_free_;
            int nextf = context_struct_.mesh_list_[lastf].next_free;
            while (nextf < mesh) {
                lastf = nextf;
                nextf = context_struct_.mesh_list_[lastf].next_free;
            }
            context_struct_.mesh_list_[lastf].next_free = mesh;
            context_struct_.mesh_list_[mesh].next_free = nextf;
        }
    }
}

/*!
 * This function sets the direction of light. If it is called with (0, 0, 0), 
 * the light is always pointing into the same direction as the camera.
 * \param [in] x The x-component of the light's direction
 * \param [in] y The y-component of the light's direction
 * \param [in] z The z-component of the light's direction
 * 
 */
GR3API void gr3_setlightdirection(float x, float y, float z) {
    if (!context_struct_.is_initialized) {
        return;
    }
    context_struct_.light_dir[0] = x;
    context_struct_.light_dir[1] = y;
    context_struct_.light_dir[2] = z;
}

/*!
 * This function sets the view matrix by getting the position of the camera, the 
 * position of the center of focus and the direction which should point up. This 
 * function takes effect when the next image is created. Therefore if you want 
 * to take pictures of the same data from different perspectives, you can call 
 * and  gr3_cameralookat(), gr3_getpixmap_(), gr3_cameralookat(), 
 * gr3_getpixmap_(), ... without calling gr3_clear() and gr3_drawmesh() again.
 * \param [in] camera_x The x-coordinate of the camera
 * \param [in] camera_y The y-coordinate of the camera
 * \param [in] camera_z The z-coordinate of the camera
 * \param [in] center_x The x-coordinate of the center of focus
 * \param [in] center_y The y-coordinate of the center of focus
 * \param [in] center_z The z-coordinate of the center of focus
 * \param [in] up_x The x-component of the up direction
 * \param [in] up_y The y-component of the up direction
 * \param [in] up_z The z-component of the up direction
 * \note Source: http://www.opengl.org/sdk/docs/man/xhtml/gluLookAt.xml 
 * (as of 10/24/2011, licensed under SGI Free Software Licence B)
 */
GR3API void gr3_cameralookat(float camera_x, float camera_y, float camera_z, 
                             float center_x, float center_y, float center_z, 
                             float up_x,  float up_y,  float up_z) {
    int i, j;
    GLfloat view_matrix[4][4] = {{0}};
    GLfloat camera_pos[3];
    GLfloat center_pos[3];
    GLfloat up_dir[3];

    GLfloat F[3];
    GLfloat f[3];
    GLfloat up[3];
    GLfloat s[3];
    GLfloat u[3];
    GLfloat tmp;
    
    if (!context_struct_.is_initialized) {
        return;
    }
    context_struct_.camera_x = camera_x;
    context_struct_.camera_y = camera_y;
    context_struct_.camera_z = camera_z;
    context_struct_.center_x = center_x;
    context_struct_.center_y = center_y;
    context_struct_.center_z = center_z;
    context_struct_.up_x     = up_x;
    context_struct_.up_y     = up_y;
    context_struct_.up_z     = up_z;
    camera_pos[0] = camera_x;
    camera_pos[1] = camera_y;
    camera_pos[2] = camera_z;
    center_pos[0] = center_x;
    center_pos[1] = center_y;
    center_pos[2] = center_z;
    up_dir[0] = up_x;
    up_dir[1] = up_y;
    up_dir[2] = up_z;

            
    for (i = 0; i < 3; i++) {
        F[i] = center_pos[i]-camera_pos[i];
    }
    /* f = normalize(F); */
    tmp = 0;
    for (i = 0; i < 3; i++) {
        tmp+= F[i]*F[i];
    }
    tmp = sqrt(tmp);
    for (i = 0; i < 3; i++) {
        f[i] = F[i]/tmp;
    }
    /* up = normalize(up_dir); */
    tmp = 0;
    for (i = 0; i < 3; i++) {
        tmp+= up_dir[i]*up_dir[i];
    }
    tmp = sqrt(tmp);
    for (i = 0; i < 3; i++) {
        up[i] = up_dir[i]/tmp;
    }
    /* s = cross(f,up); */
    for (i = 0; i < 3; i++) {
        s[i] = f[(i+1)%3]*up[(i+2)%3] - up[(i+1)%3]*f[(i+2)%3];
    }
    /* s = normalize(s); */
    tmp = 0;
    for (i = 0; i < 3; i++) {
        tmp+= s[i]*s[i];
    }
    tmp = sqrt(tmp);
    for (i = 0; i < 3; i++) {
        s[i] = s[i]/tmp;
    }
    /* u = cross(s,f); */
    for (i = 0; i < 3; i++) {
        u[i] = s[(i+1)%3]*f[(i+2)%3] - f[(i+1)%3]*s[(i+2)%3];
    }
    
    /* u = normalize(u); */
    tmp = 0;
    for (i = 0; i < 3; i++) {
        tmp+= u[i]*u[i];
    }
    tmp = sqrt(tmp);
    for (i = 0; i < 3; i++) {
        u[i] = u[i]/tmp;
    }
    for (i = 0; i < 3; i++) {
        view_matrix[i][0] = s[i];
        view_matrix[i][1] = u[i];
        view_matrix[i][2] = -f[i];
    }
    view_matrix[3][3] = 1;
    for (i = 0; i < 3; i++) {
        view_matrix[3][i] = 0;
        for (j = 0; j < 3; j++) {
            view_matrix[3][i] -= view_matrix[j][i]*camera_pos[j];
        }
    }
    memcpy(&context_struct_.view_matrix[0][0],&view_matrix[0][0],sizeof(view_matrix));
}

/*!
 * This function sets the projection parameters. This function takes effect 
 * when the next image is created.
 * \param [in] vertical_field_of_view   This parameter is the vertical field of 
 *                                      view in degrees. It must be greater than 
 *                                      0 and less than 180.
 * \param [in] zNear                    The distance to the near clipping plane.
 * \param [in] zFar                     The distance to the far clipping plane.
 * \returns
 * - ::GR3_ERROR_NONE                   on success
 * - ::GR3_ERROR_INVALID_VALUE          if one (or more) of the arguments is out of its range
 * \note The ratio between zFar and zNear influences the precision of the depth 
 * buffer, the greater \f$ \frac{zFar}{zNear} \f$, the more likely are errors. So
 * you should try to keep both values as close to each other as possible while 
 * making sure everything you want to be visible, is visible.
 */
GR3API int gr3_setcameraprojectionparameters(float vertical_field_of_view, 
                                             float zNear, float zFar) {
    if (!context_struct_.is_initialized) {
        return GR3_ERROR_NOT_INITIALIZED;
    }
    
    if (zFar < zNear || zNear <= 0 || vertical_field_of_view >= 180 || vertical_field_of_view <= 0) {
        return GR3_ERROR_INVALID_VALUE;
    }
    context_struct_.vertical_field_of_view = vertical_field_of_view;
    context_struct_.zNear = zNear;
    context_struct_.zFar = zFar;
    return GR3_ERROR_NONE;
}

/*!
 * This function iterates over the draw list and draws the image using OpenGL.
 */
static void gr3_draw_(GLuint width, GLuint height) {
#ifdef GR3_CAN_USE_VBO
    if (context_struct_.use_vbo) {
        glUseProgram(context_struct_.program);
    }
#endif
    gr3_log_("gr3_draw_();");
    {
        GLfloat projection_matrix[4][4] = {{0}};
        GLfloat *pm;
        if (context_struct_.projection_matrix != NULL) {
            pm = context_struct_.projection_matrix;
        } else {
            GLfloat fovy = context_struct_.vertical_field_of_view;
            GLfloat zNear = context_struct_.zNear;
            GLfloat zFar = context_struct_.zFar;
            
            
            {
                /* Source: http://www.opengl.org/sdk/docs/man/xhtml/gluPerspective.xml */
                GLfloat aspect = (GLfloat)width/height;
                GLfloat f = 1/tan(fovy*M_PI/360.0);
                projection_matrix[0][0] = f/aspect;
                projection_matrix[1][1] = f;
                projection_matrix[2][2] = (zFar+zNear)/(zNear-zFar);
                projection_matrix[3][2] = 2*zFar*zNear/(zNear-zFar);
                projection_matrix[2][3] = -1;
            }
            pm = &projection_matrix[0][0];
        }
#ifdef GR3_CAN_USE_VBO
        if (context_struct_.use_vbo) {
            glUniformMatrix4fv(glGetUniformLocation(context_struct_.program, "ProjectionMatrix"), 1,GL_FALSE,pm);
        } else
#endif
        {
            glMatrixMode(GL_PROJECTION);
            glLoadMatrixf(pm);
        }

#ifdef GR3_CAN_USE_VBO
        if (context_struct_.use_vbo) {
            glUniformMatrix4fv(glGetUniformLocation(context_struct_.program, "ViewMatrix"), 1,GL_FALSE,&(context_struct_.view_matrix[0][0]));
        } else 
#endif
        {
            glMatrixMode(GL_MODELVIEW);
            if (context_struct_.light_dir[0] == 0 && 
                context_struct_.light_dir[1] == 0 && 
                context_struct_.light_dir[2] == 0
            ) {
                GLfloat def[4] = {0,0,1,0};
                glLoadIdentity();
                glLightfv(GL_LIGHT0, GL_POSITION, &def[0]);
            }
            glLoadMatrixf(&(context_struct_.view_matrix[0][0]));
        }
#ifdef GR3_CAN_USE_VBO
        if (context_struct_.use_vbo) {
            glUniform3f(glGetUniformLocation(context_struct_.program, "LightDirection"),context_struct_.light_dir[0],context_struct_.light_dir[1],context_struct_.light_dir[2]);
        }
#endif
    }
    glEnable(GL_NORMALIZE);
    if (!context_struct_.use_vbo) {
        glEnable(GL_LIGHTING);
        glEnable(GL_LIGHT0);
        if (context_struct_.light_dir[0] != 0 || 
            context_struct_.light_dir[1] != 0 || 
            context_struct_.light_dir[2] != 0
        ) {
            glLightfv(GL_LIGHT0, GL_POSITION, &context_struct_.light_dir[0]);
        }
    }
    glClearColor(context_struct_.background_color[0], context_struct_.background_color[1], context_struct_.background_color[2], context_struct_.background_color[3]);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    {
        GR3_DrawList_t_ *draw;
        draw = context_struct_.draw_list_;
        while (draw) {
            gr3_dodrawmesh_(draw->mesh,draw->n,draw->positions,draw->directions,
                            draw->ups,draw->colors,draw->scales);
            draw = draw->next;
        }
    }
#ifdef GR3_CAN_USE_VBO
    if (context_struct_.use_vbo) {
        glUseProgram(0);
    }
#endif
}

GR3API void gr3_renderdirect(int width, int height) {
    gr3_log_("gr3_renderdirect();");
#if GL_ARB_framebuffer_object
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
#else
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
#endif
    glViewport(0,0,width,height);
    gr3_draw_(width, height);
}

GR3API int gr3_drawscene(float xmin, float xmax, float ymin, float ymax, int pixelWidth, int pixelHeight) {
    char *pixels;
    int err;
    gr3_log_("gr3_drawscene();");
    pixelWidth = 800;
    pixelHeight = 800;
    pixels = (char *)malloc(sizeof(int)*pixelWidth*pixelHeight);
    if (!pixels) {
        return GR3_ERROR_OUT_OF_MEM;
    }
    err = gr3_getimage(pixelWidth,pixelHeight,TRUE,pixels);
    if (err != GR3_ERROR_NONE) {
        free(pixels);
        return err;
    }
    gr_drawimage(xmin, xmax, ymax, ymin, pixelWidth, pixelHeight, (int *)pixels);
    free(pixels);
    return GR3_ERROR_NONE;
}

static int gr3_strendswith_(const char *str, const char *ending) {
    int str_len = strlen(str);
    int ending_len = strlen(ending);
    return (str_len >= ending_len) && !strcmp(str+str_len-ending_len,ending);
}

GR3API int gr3_setquality(int quality) {
    int ssaa_factor = quality & ~1;
    int i;
    if (quality > 33 || quality < 0) {
        return GR3_ERROR_INVALID_VALUE;
    }
    if (ssaa_factor == 0) ssaa_factor = 1;
    i = ssaa_factor;
    while ( i/2*2 == i) {
        i = i/2;
    }
    if (i != 1) {
        return GR3_ERROR_INVALID_VALUE;
    }
    context_struct_.quality = quality;
    fprintf(stderr,"setquality(%d);\n",quality);
    return GR3_ERROR_NONE;
}

GR3API int gr3_getimage(int width, int height, int use_alpha, char *pixels) {
    int err;
    int quality = context_struct_.quality;
    int ssaa_factor = quality & ~1;
    int use_povray = quality & 1;
    if (ssaa_factor == 0) ssaa_factor = 1;
    if (use_povray) {
        err = gr3_getpovray_(pixels,width, height, use_alpha, ssaa_factor);
    } else {
        err = gr3_getpixmap_(pixels,width, height, use_alpha, ssaa_factor);
    }
    return err;
}

GR3API int gr3_export(const char *filename, int width, int height) {
    
    gr3_log_(filename);
    
    if (gr3_strendswith_(filename, ".html")) {
        gr3_log_("export as html file");
        return gr3_export_html_(filename, width, height);
    } else if (gr3_strendswith_(filename, ".pov")) {
        gr3_log_("export as pov file");
        return gr3_export_pov_(filename, width, height);
    } else if (gr3_strendswith_(filename, ".png")) {
        gr3_log_("export as png file");
        return gr3_export_png_(filename, width, height);
    } else if (gr3_strendswith_(filename, ".jpg") || gr3_strendswith_(filename, ".jpeg")) {
        gr3_log_("export as jpeg file");
        return gr3_export_jpeg_(filename, width, height);
    }
    
    return GR3_ERROR_UNKNOWN_FILE_EXTENSION;
}

static int gr3_export_jpeg_(const char *filename, int width, int height) {
    FILE *jpegfp;
    char *pixels;
    int err;
    
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    
    jpegfp = fopen(filename, "wb");
    if (!jpegfp) {
        return GR3_ERROR_CANNOT_OPEN_FILE;
    }
    
    pixels = (char *)malloc(width * height * 3);
    if (!pixels) {
        return GR3_ERROR_OUT_OF_MEM;
    }
    
    err = gr3_getimage(width, height, FALSE, pixels);
    if (err != GR3_ERROR_NONE) {
        fclose(jpegfp);
        free(pixels);
        return err;
    }
    
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);
    jpeg_stdio_dest(&cinfo, jpegfp);
    
    cinfo.image_width = width;
    cinfo.image_height = height;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, 100, TRUE);
    jpeg_start_compress(&cinfo, TRUE);
    while (cinfo.next_scanline < cinfo.image_height) {
        JSAMPROW row_pointer[1];
        row_pointer[0] = (JSAMPLE *)(pixels+3*(height-cinfo.next_scanline-1)*width);
        jpeg_write_scanlines(&cinfo, row_pointer, 1);
    }
    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
    fclose(jpegfp);
    free(pixels);
    return GR3_ERROR_NONE;
}

static int gr3_export_png_(const char *filename, int width, int height) {
    FILE *pngfp;
    int *pixels;
    int err;
    int i;
    
    png_structp png_ptr;
    png_infop info_ptr;
    png_bytepp row_pointers;
    
    pngfp = fopen(filename, "wb");
    if (!pngfp) {
        return GR3_ERROR_CANNOT_OPEN_FILE;
    }
    
    pixels = (int *)malloc(width * height * sizeof(int));
    if (!pixels) {
        return GR3_ERROR_OUT_OF_MEM;
    }
    
    err = gr3_getimage(width, height, TRUE, (char *)pixels);
    if (err != GR3_ERROR_NONE) {
        fclose(pngfp);
        free(pixels);
        return err;
    }
    
    png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr) {
        fclose(pngfp);
        free(pixels);
        return GR3_ERROR_EXPORT;
    }
    info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        fclose(pngfp);
        free(pixels);
        png_destroy_write_struct(&png_ptr, (png_infopp)NULL);
        return GR3_ERROR_EXPORT;
    }
    png_init_io(png_ptr, pngfp);
    png_set_IHDR(png_ptr, info_ptr, width, height, 8, PNG_COLOR_TYPE_RGB_ALPHA, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    
    row_pointers = malloc(height*sizeof(png_bytep));
    for (i = 0; i < height; i++) {
        row_pointers[i]=(png_bytep)(pixels+(height-i-1)*width);
    }
    png_set_rows(png_ptr, info_ptr, (void *)row_pointers);
    png_write_png(png_ptr, info_ptr, PNG_TRANSFORM_IDENTITY, NULL);
    
    png_destroy_write_struct(&png_ptr, &info_ptr);
    fclose(pngfp);
    free(row_pointers);
    free(pixels);
    return GR3_ERROR_NONE;
}

static int gr3_export_pov_(const char *filename, int width, int height) {
    int i, j, k, l;
    FILE *povfp;
    GR3_DrawList_t_ *draw;
    
    /* dummy */ width = height = width; /* dummy */
    
    povfp = fopen(filename, "w");
    if (!povfp) {
        return GR3_ERROR_CANNOT_OPEN_FILE;
    }
    
    fprintf(povfp,"camera {\n");
    fprintf(povfp,"  up <0,1,0>\n");
    fprintf(povfp,"  right <-1,0,0>\n");
    fprintf(povfp,"  location <%f, %f, %f>\n", context_struct_.camera_x, context_struct_.camera_y, context_struct_.camera_z);
    fprintf(povfp,"  look_at <%f, %f, %f>\n", context_struct_.center_x, context_struct_.center_y, context_struct_.center_z);
    fprintf(povfp,"  sky <%f, %f, %f>\n", context_struct_.up_x, context_struct_.up_y, context_struct_.up_z);
    fprintf(povfp,"  angle %f\n", context_struct_.vertical_field_of_view);
    fprintf(povfp,"}\n");
    
    if (context_struct_.light_dir[0] == 0 && 
        context_struct_.light_dir[1] == 0 && 
        context_struct_.light_dir[2] == 0
        ) {
        GLfloat camera_pos[3];
        camera_pos[0] = context_struct_.camera_x;
        camera_pos[1] = context_struct_.camera_y;
        camera_pos[2] = context_struct_.camera_z;
        fprintf(povfp,"light_source { <%f, %f, %f> color rgb <1.0, 1.0, 1.0> }\n",camera_pos[0], camera_pos[1],camera_pos[2]);
    } else {
        fprintf(povfp,"light_source { <%f, %f, %f> color rgb <1.0, 1.0, 1.0> }\n",context_struct_.light_dir[0], context_struct_.light_dir[1],context_struct_.light_dir[2]);
    }
fprintf(povfp,"background { color rgb <%f, %f, %f> }\n", context_struct_.background_color[0], context_struct_.background_color[1], context_struct_.background_color[2]);
    draw = context_struct_.draw_list_;
    while (draw) {
        switch(context_struct_.mesh_list_[draw->mesh].data.type) {
            case kMTSphereMesh:
                for(i = 0; i < draw->n; i++) {
                    fprintf(povfp,"sphere {\n");
                    fprintf(povfp,"  <%f, %f, %f>, %f\n",draw->positions[i*3+0],draw->positions[i*3+1],draw->positions[i*3+2],draw->scales[i*3+0]);
                    fprintf(povfp,"  texture {\n");
                    fprintf(povfp,"    pigment { color rgb <%f, %f, %f> }\n",draw->colors[i*3+0],draw->colors[i*3+1],draw->colors[i*3+2]);
                    fprintf(povfp,"  }\n");
                    fprintf(povfp,"}\n");
                }
                break;
            case kMTCylinderMesh:
                for(i = 0; i < draw->n; i++) {
                    float len_sq = draw->directions[i*3+0]*draw->directions[i*3+0] + draw->directions[i*3+1]*draw->directions[i*3+1] + draw->directions[i*3+2]*draw->directions[i*3+2];
                    float len = sqrt(len_sq);
                    fprintf(povfp,"cylinder {\n");
                    fprintf(povfp,"  <%f, %f, %f>, <%f, %f, %f>, %f\n",draw->positions[i*3+0],draw->positions[i*3+1],draw->positions[i*3+2],draw->positions[i*3+0]+draw->directions[i*3+0]/len*draw->scales[i*3+2],draw->positions[i*3+1]+draw->directions[i*3+1]/len*draw->scales[i*3+2],draw->positions[i*3+2]+draw->directions[i*3+2]/len*draw->scales[i*3+2],draw->scales[i*3+0]);
                    fprintf(povfp,"  texture {\n");
                    fprintf(povfp,"    pigment { color rgb <%f, %f, %f> }\n",draw->colors[i*3+0],draw->colors[i*3+1],draw->colors[i*3+2]);
                    fprintf(povfp,"  }\n");
                    fprintf(povfp,"}\n");
                }
                break;
            case kMTConeMesh:
                for(i = 0; i < draw->n; i++) {
                    float len_sq = draw->directions[i*3+0]*draw->directions[i*3+0] + draw->directions[i*3+1]*draw->directions[i*3+1] + draw->directions[i*3+2]*draw->directions[i*3+2];
                    float len = sqrt(len_sq);
                    fprintf(povfp,"cone {\n");
                    fprintf(povfp,"  <%f, %f, %f>, %f, <%f, %f, %f>, %f\n",draw->positions[i*3+0],draw->positions[i*3+1],draw->positions[i*3+2],draw->scales[i*3+0],draw->positions[i*3+0]+draw->directions[i*3+0]/len*draw->scales[i*3+2],draw->positions[i*3+1]+draw->directions[i*3+1]/len*draw->scales[i*3+2],draw->positions[i*3+2]+draw->directions[i*3+2]/len*draw->scales[i*3+2],0.0);
                    fprintf(povfp,"  texture {\n");
                    fprintf(povfp,"    pigment { color rgb <%f, %f, %f> }\n",draw->colors[i*3+0],draw->colors[i*3+1],draw->colors[i*3+2]);
                    fprintf(povfp,"  }\n");
                    fprintf(povfp,"}\n");
                }
                break;
            case kMTNormalMesh:
                for(i = 0; i < draw->n; i++) {
                    GLfloat model_matrix[4][4] = {{0}};
                    const float *vertices = context_struct_.mesh_list_[draw->mesh].data.vertices;
                    const float *normals = context_struct_.mesh_list_[draw->mesh].data.normals;
                    const float *colors = context_struct_.mesh_list_[draw->mesh].data.colors;
                    {
                        int m;
                        GLfloat forward[3], up[3], left[3];
                        float tmp;
                        /* Calculate an orthonormal base in IR^3, correcting the up vector 
                         * in case it is not perpendicular to the forward vector. This base
                         * is used to create the model matrix as a base-transformation 
                         * matrix.
                         */
                        /* forward = normalize(&directions[i*3]); */
                        tmp = 0;
                        for (m = 0; m < 3; m++) {
                            tmp+= draw->directions[i*3+m]*draw->directions[i*3+m];
                        }
                        tmp = sqrt(tmp);
                        for (m = 0; m < 3; m++) {
                            forward[m] = draw->directions[i*3+m]/tmp;
                        }/* up = normalize(&ups[i*3]); */
                        tmp = 0;
                        for (m = 0; m < 3; m++) {
                            tmp+= draw->ups[i*3+m]*draw->ups[i*3+m];
                        }
                        tmp = sqrt(tmp);
                        for (m = 0; m < 3; m++) {
                            up[m] = draw->ups[i*3+m]/tmp;
                        }
                        /* left = cross(forward,up); */
                        for (m = 0; m < 3; m++) {
                            left[m] = forward[(m+1)%3]*up[(m+2)%3] - up[(m+1)%3]*forward[(m+2)%3];
                        }
                        /* up = cross(left,forward); */
                        for (m = 0; m < 3; m++) {
                            up[m] = left[(m+1)%3]*forward[(m+2)%3] - forward[(m+1)%3]*left[(m+2)%3];
                        }
                        for (m = 0; m < 3; m++) {
                            model_matrix[0][m] = -left[m];
                            model_matrix[1][m] = up[m];
                            model_matrix[2][m] = forward[m];
                            model_matrix[3][m] = draw->positions[i*3+m];
                        }
                        model_matrix[3][3] = 1;
                    }
                    fprintf(povfp,"mesh {\n");
                    for (j = 0; j < context_struct_.mesh_list_[draw->mesh].data.number_of_vertices/3; j++) {
                        fprintf(povfp,"#local tex = texture { pigment { color rgb <%f, %f, %f> } }\n",draw->colors[i*3+0]*colors[j*3+0],draw->colors[i*3+1]*colors[j*3+1],draw->colors[i*3+2]*colors[j*3+2]);
                        fprintf(povfp,"  smooth_triangle {\n");
                        for (k = 0; k < 3; k++) {
                            float vertex1[4];
                            float vertex2[4];
                            float normal1[3];
                            float normal2[3];
                            for (l = 0; l < 3; l++) {
                                vertex1[l] = draw->scales[i*3+l]*vertices[j*9+k*3+l];
                            }
                            vertex1[3] = 1;
                            for (l = 0; l < 4; l++) {
                                vertex2[l] = model_matrix[0][l]*vertex1[0]+model_matrix[1][l]*vertex1[1]+model_matrix[2][l]*vertex1[2]+model_matrix[3][l]*vertex1[3];
                            }
                            for (l = 0; l < 3; l++) {
                                normal1[l] = normals[j*9+k*3+l];
                            }
                            vertex1[3] = 1;
                            for (l = 0; l < 4; l++) {
                                normal2[l] = model_matrix[0][l]*normal1[0]+model_matrix[1][l]*normal1[1]+model_matrix[2][l]*normal1[2];
                            }
                            fprintf(povfp,"    <%f, %f, %f>,",vertex2[0],vertex2[1],vertex2[2]);
                            fprintf(povfp," <%f, %f, %f>",normal2[0],normal2[1],normal2[2]);
                            if (k < 2) {
                                fprintf(povfp,",");
                            }
                            fprintf(povfp,"\n");
                        }
                        fprintf(povfp,"    texture { tex }\n");
                        fprintf(povfp,"  }\n");
                    }
                    fprintf(povfp,"}\n");
                }
                break;
            default:
                gr3_log_("Unknown mesh type");
                break;
        }
        draw = draw->next;
    }
    fclose(povfp);
    return 0;
}

static int gr3_export_html_(const char *filename, int width, int height) {
    FILE *htmlfp;
    const char *title = "GR3";
    int i, j;
    
    htmlfp = fopen(filename, "w");
    if (!htmlfp) {
        return GR3_ERROR_CANNOT_OPEN_FILE;
    }
    
    fprintf(htmlfp, "<!DOCTYPE html>\n");
    fprintf(htmlfp, "<html>\n");
    fprintf(htmlfp, "  <head>\n");
    fprintf(htmlfp, "    <meta charset=\"utf-8\" />\n");
    fprintf(htmlfp, "    <title>%s</title>\n", title);
    fprintf(htmlfp, "    <script type=\"text/javascript\">\n");
    
    fprintf(htmlfp, "      function startWebGLCanvas() {\n");
    fprintf(htmlfp, "        var canvas = document.getElementById(\"webgl-canvas\");\n");
    fprintf(htmlfp, "        initWebGL(canvas);\n");
    fprintf(htmlfp, "        initShaderProgram();\n");
    fprintf(htmlfp, "        initMeshes();\n");
    fprintf(htmlfp, "        \n");
    fprintf(htmlfp, "        gl.enable(gl.DEPTH_TEST);\n");
    fprintf(htmlfp, "        \n");
    fprintf(htmlfp, "        drawScene();\n");
    fprintf(htmlfp, "        canvas.onmousemove = canvasMouseMove;\n");
    fprintf(htmlfp, "        canvas.onmouseup = canvasMouseUp;\n");
    fprintf(htmlfp, "        canvas.onmousedown = canvasMouseDown;\n");
    fprintf(htmlfp, "        canvas.onmouseout = canvasMouseOut;\n");
    fprintf(htmlfp, "        canvas.onkeypress = canvasKeyPress;\n");
    fprintf(htmlfp, "      }\n");
    
    fprintf(htmlfp, "      function transposeMatrix4(matrix) {\n");
    fprintf(htmlfp, "        var transposedMatrix = [");
    fprintf(htmlfp, "          matrix[0],  matrix[4],  matrix[8],  matrix[12],\n");
    fprintf(htmlfp, "          matrix[1],  matrix[5],  matrix[9],  matrix[13],\n");
    fprintf(htmlfp, "          matrix[2],  matrix[6],  matrix[10], matrix[14],\n");
    fprintf(htmlfp, "          matrix[3],  matrix[7],  matrix[11], matrix[15]\n");
    fprintf(htmlfp, "        ];\n");
    fprintf(htmlfp, "        return transposedMatrix;\n");
    fprintf(htmlfp, "      }\n");
    
    fprintf(htmlfp, "      var gl;\n");
    fprintf(htmlfp, "      function initWebGL(canvas) {\n");
    fprintf(htmlfp, "        try {\n");
    fprintf(htmlfp, "          gl = canvas.getContext(\"experimental-webgl\", {antialias: true, stencil: false});\n");
    fprintf(htmlfp, "          gl.viewportWidth = canvas.width;\n");
    fprintf(htmlfp, "          gl.viewportHeight = canvas.height;\n");
    fprintf(htmlfp, "        } catch(e) {\n");
    fprintf(htmlfp, "        }\n");
    fprintf(htmlfp, "        if (!gl) {\n");
    fprintf(htmlfp, "          alert(\"Unable to initialize WebGL.\");\n");
    fprintf(htmlfp, "        }\n");
    fprintf(htmlfp, "      }\n");
    
    
    fprintf(htmlfp, "      var meshes;\n");
    fprintf(htmlfp, "      function initMeshes() {\n");
    fprintf(htmlfp, "        function Mesh(id, vertices, normals, colors) {\n");
    fprintf(htmlfp, "          this.id = id;\n");
    fprintf(htmlfp, "          this.vertices = vertices;\n");
    fprintf(htmlfp, "          this.normals = normals;\n");
    fprintf(htmlfp, "          this.colors = colors;\n");
    fprintf(htmlfp, "          \n");
    fprintf(htmlfp, "          this.init = function () {\n");
    fprintf(htmlfp, "            this.vertex_buffer = gl.createBuffer()\n");
    fprintf(htmlfp, "            gl.bindBuffer(gl.ARRAY_BUFFER, this.vertex_buffer);\n");
    fprintf(htmlfp, "            gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(this.vertices), gl.STATIC_DRAW);\n");
    fprintf(htmlfp, "            this.normal_buffer = gl.createBuffer()\n");
    fprintf(htmlfp, "            gl.bindBuffer(gl.ARRAY_BUFFER, this.normal_buffer);\n");
    fprintf(htmlfp, "            gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(this.normals), gl.STATIC_DRAW);\n");
    fprintf(htmlfp, "            this.color_buffer = gl.createBuffer()\n");
    fprintf(htmlfp, "            gl.bindBuffer(gl.ARRAY_BUFFER, this.color_buffer);\n");
    fprintf(htmlfp, "            gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(this.colors), gl.STATIC_DRAW);\n");
    fprintf(htmlfp, "            this.number_of_vertices = vertices.length/3;\n");
    fprintf(htmlfp, "            \n");
    fprintf(htmlfp, "          }\n");
    fprintf(htmlfp, "          this.bind = function () {\n");
    fprintf(htmlfp, "            gl.bindBuffer(gl.ARRAY_BUFFER, this.vertex_buffer);\n");
    fprintf(htmlfp, "            gl.vertexAttribPointer(shaderProgram.vertexLocation, 3, gl.FLOAT, false, 0, 0);\n");
    fprintf(htmlfp, "            gl.enableVertexAttribArray(shaderProgram.vertexLocation);\n");
    fprintf(htmlfp, "            gl.bindBuffer(gl.ARRAY_BUFFER, this.normal_buffer);\n");
    fprintf(htmlfp, "            gl.vertexAttribPointer(shaderProgram.normalLocation, 3, gl.FLOAT, false, 0, 0);\n");
    fprintf(htmlfp, "            gl.enableVertexAttribArray(shaderProgram.normalLocation);\n");
    fprintf(htmlfp, "            gl.bindBuffer(gl.ARRAY_BUFFER, this.color_buffer);\n");
    fprintf(htmlfp, "            gl.vertexAttribPointer(shaderProgram.colorLocation, 3, gl.FLOAT, false, 0, 0);\n");
    fprintf(htmlfp, "            gl.enableVertexAttribArray(shaderProgram.colorLocation);\n");
    fprintf(htmlfp, "            \n");
    fprintf(htmlfp, "          }\n");
    fprintf(htmlfp, "          this.draw = function (projectionMatrix, viewMatrix, modelMatrix, scales, lightDirection) {\n");
    fprintf(htmlfp, "            gl.uniformMatrix4fv(shaderProgram.projectionMatrixLocation, false, new Float32Array(projectionMatrix));\n");
    fprintf(htmlfp, "            gl.uniformMatrix4fv(shaderProgram.viewMatrixLocation, false, new Float32Array(viewMatrix));\n");
    fprintf(htmlfp, "            gl.uniformMatrix4fv(shaderProgram.modelMatrixLocation, false, new Float32Array(modelMatrix));\n");
    fprintf(htmlfp, "            gl.uniform3fv(shaderProgram.scalesLocation, new Float32Array(scales));\n");
    fprintf(htmlfp, "            gl.uniform3fv(shaderProgram.lightDirectionLocation, new Float32Array(lightDirection));\n");
    fprintf(htmlfp, "            \n");
    fprintf(htmlfp, "            gl.drawArrays(gl.TRIANGLES, 0, this.number_of_vertices);\n");
    fprintf(htmlfp, "            \n");
    fprintf(htmlfp, "          }\n");
    fprintf(htmlfp, "        }\n");
    fprintf(htmlfp, "        \n");
    fprintf(htmlfp, "        meshes = new Array();\n");
    for (i = 0; i < context_struct_.mesh_list_capacity_; i++) {
        if (context_struct_.mesh_list_[i].refcount > 0) {
            fprintf(htmlfp, "        var vertices = [\n");
            for (j = 0; j < context_struct_.mesh_list_[i].data.number_of_vertices; j++) {
                fprintf(htmlfp, "          %f, %f, %f", context_struct_.mesh_list_[i].data.vertices[3*j+0], context_struct_.mesh_list_[i].data.vertices[3*j+1], context_struct_.mesh_list_[i].data.vertices[3*j+2]);
                if (j + 1 < context_struct_.mesh_list_[i].data.number_of_vertices) {
                    fprintf(htmlfp, ",\n");
                }
            }
            fprintf(htmlfp, "        ];\n");
            fprintf(htmlfp, "        var normals = [\n");
            for (j = 0; j < context_struct_.mesh_list_[i].data.number_of_vertices; j++) {
                fprintf(htmlfp, "          %f, %f, %f", context_struct_.mesh_list_[i].data.normals[3*j+0], context_struct_.mesh_list_[i].data.normals[3*j+1], context_struct_.mesh_list_[i].data.normals[3*j+2]);
                if (j + 1 < context_struct_.mesh_list_[i].data.number_of_vertices) {
                    fprintf(htmlfp, ",\n");
                }
            }
            fprintf(htmlfp, "        ];\n");
            fprintf(htmlfp, "        var colors = [\n");
            for (j = 0; j < context_struct_.mesh_list_[i].data.number_of_vertices; j++) {
                fprintf(htmlfp, "          %f, %f, %f", context_struct_.mesh_list_[i].data.colors[3*j+0], context_struct_.mesh_list_[i].data.colors[3*j+1], context_struct_.mesh_list_[i].data.colors[3*j+2]);
                if (j + 1 < context_struct_.mesh_list_[i].data.number_of_vertices) {
                    fprintf(htmlfp, ",\n");
                }
            }
            fprintf(htmlfp, "        ];\n");
            fprintf(htmlfp, "        \n");
            fprintf(htmlfp, "        var mesh = new Mesh(%u, vertices, normals, colors);\n", i);
            fprintf(htmlfp, "        mesh.init();\n");
            fprintf(htmlfp, "        meshes.push(mesh);\n");
        }
    }
    fprintf(htmlfp, "      }\n");
    
    
    
    fprintf(htmlfp, "      function getRotationMatrix(angle, x, y, z) {\n");
    fprintf(htmlfp, "        var f = Math.PI/180;\n");
    fprintf(htmlfp, "        var s = Math.sin(angle);\n");
    fprintf(htmlfp, "        var c = Math.cos(angle);\n");
    fprintf(htmlfp, "        var matrix = [x*x*(1-c)+c,     x*y*(1-c)-z*s,   x*z*(1-c)+y*s, 0,\n");
    fprintf(htmlfp, "                      y*x*(1-c)+z*s,   y*y*(1-c)+c,     y*z*(1-c)-x*s, 0,\n");
    fprintf(htmlfp, "                      x*z*(1-c)-y*s,   y*z*(1-c)+x*s,   z*z*(1-c)+c,   0,\n");
    fprintf(htmlfp, "                                  0,               0,             0,   1];\n");
    fprintf(htmlfp, "        \n");
    fprintf(htmlfp, "        return matrix;\n");
    fprintf(htmlfp, "      }\n");
    
    fprintf(htmlfp, "      function multMatrix4(matrix1, matrix2) {\n");
    fprintf(htmlfp, "        var matrix = [0,0,0,0,\n");
    fprintf(htmlfp, "                      0,0,0,0,\n");
    fprintf(htmlfp, "                      0,0,0,0,\n");
    fprintf(htmlfp, "                      0,0,0,0];\n");
    fprintf(htmlfp, "        var i, j, k;\n");
    fprintf(htmlfp, "        for (i = 0; i < 4; i++) {\n");
    fprintf(htmlfp, "          for (j = 0; j < 4; j++) {\n");
    fprintf(htmlfp, "            matrix[i+4*j] = 0;\n");
    fprintf(htmlfp, "            for (k = 0; k < 4; k++) {\n");
    fprintf(htmlfp, "              matrix[i+4*j] = matrix[i+4*j] + matrix1[j*4+k]*matrix2[k*4+i];\n");
    fprintf(htmlfp, "            }\n");
    fprintf(htmlfp, "          }\n");
    fprintf(htmlfp, "        }\n");
    fprintf(htmlfp, "        return matrix;\n");
    fprintf(htmlfp, "      }\n");
    
    fprintf(htmlfp, "      var isDragging = false;\n");
    fprintf(htmlfp, "      var xOffset = 0;\n");
    fprintf(htmlfp, "      var yOffset = 0;\n");
    fprintf(htmlfp, "      function canvasMouseUp(event) {\n");
    fprintf(htmlfp, "        isDragging = false;\n");
    fprintf(htmlfp, "        xOffset = event.clientX;\n");
    fprintf(htmlfp, "        yOffset = event.clientY;\n");
    fprintf(htmlfp, "      }\n");
    fprintf(htmlfp, "      function canvasMouseDown(event) {\n");
    fprintf(htmlfp, "        isDragging = true;\n");
    fprintf(htmlfp, "        xOffset = event.clientX;\n");
    fprintf(htmlfp, "        yOffset = event.clientY;\n");
    fprintf(htmlfp, "      }\n");
    fprintf(htmlfp, "      function canvasMouseOut(event) {\n");
    fprintf(htmlfp, "        isDragging = false;\n");
    fprintf(htmlfp, "      }\n");
    fprintf(htmlfp, "      function canvasKeyPress(event) {\n");
    fprintf(htmlfp, "        var unicode=event.keyCode? event.keyCode : event.charCode;\n");
    fprintf(htmlfp, "        var character = String.fromCharCode(unicode);\n");
    fprintf(htmlfp, "        if (character == \"r\") {\n");
    fprintf(htmlfp, "          camera_pos = original_camera_pos.slice(0);\n");
    fprintf(htmlfp, "          center_pos = original_center_pos.slice(0);\n");
    fprintf(htmlfp, "          up_dir = original_up_dir.slice(0);\n");
    fprintf(htmlfp, "          calculateViewMatrix();\n");
    fprintf(htmlfp, "          drawScene();\n");
    fprintf(htmlfp, "        }\n");
    fprintf(htmlfp, "      }\n");
    fprintf(htmlfp, "      function canvasMouseMove(event) {\n");
    fprintf(htmlfp, "        if (isDragging) {\n");
    fprintf(htmlfp, "          dx = event.clientX-xOffset;\n");
    fprintf(htmlfp, "          dy = event.clientY-yOffset;\n");
    fprintf(htmlfp, "          if (dx == 0 && dy == 0) return;\n");
    fprintf(htmlfp, "          xOffset = event.clientX;\n");
    fprintf(htmlfp, "          yOffset = event.clientY;\n");
    fprintf(htmlfp, "          var forward = [0.0 ,0.0, 0.0];\n");
    fprintf(htmlfp, "          for (i = 0; i < 3; i++) {\n");
    fprintf(htmlfp, "            forward[i] = center_pos[i] - camera_pos[i];\n");
    fprintf(htmlfp, "          }\n");
    fprintf(htmlfp, "          var up = [0.0 ,0.0, 0.0];\n");
    fprintf(htmlfp, "          var tmp = 0;\n");
    fprintf(htmlfp, "          for (i = 0; i < 3; i++) {\n");
    fprintf(htmlfp, "            tmp = tmp + forward[i]*forward[i];\n");
    fprintf(htmlfp, "          }\n");
    fprintf(htmlfp, "          var len_forward = Math.sqrt(tmp);\n");
    fprintf(htmlfp, "          var tmp = 0;\n");
    fprintf(htmlfp, "          for (i = 0; i < 3; i++) {\n");
    fprintf(htmlfp, "            tmp = tmp + up_dir[i]*up_dir[i];\n");
    fprintf(htmlfp, "          }\n");
    fprintf(htmlfp, "          tmp = Math.sqrt(tmp);\n");
    fprintf(htmlfp, "          for (i = 0; i < 3; i++) {\n");
    fprintf(htmlfp, "            up[i] = up_dir[i]/tmp;\n");
    fprintf(htmlfp, "          }\n");
    fprintf(htmlfp, "          var right = [0.0 ,0.0, 0.0];\n");
    fprintf(htmlfp, "          for (i = 0; i < 3; i++) {\n");
    fprintf(htmlfp, "            right[i] = forward[(i+1)%%3]*up[(i+2)%%3] - up[(i+1)%%3]*forward[(i+2)%%3];\n");
    fprintf(htmlfp, "          }\n");
    fprintf(htmlfp, "          var tmp = 0;\n");
    fprintf(htmlfp, "          for (i = 0; i < 3; i++) {\n");
    fprintf(htmlfp, "            tmp = tmp + right[i]*right[i];\n");
    fprintf(htmlfp, "          }\n");
    fprintf(htmlfp, "          tmp = Math.sqrt(tmp);\n");
    fprintf(htmlfp, "          for (i = 0; i < 3; i++) {\n");
    fprintf(htmlfp, "            right[i] = right[i]/tmp;\n");
    fprintf(htmlfp, "          }\n");
    fprintf(htmlfp, "          \n");
    fprintf(htmlfp, "          var rotation = [0.0, 0.0, 0.0];\n");
    fprintf(htmlfp, "          for (i = 0; i < 3; i++) {\n");
    fprintf(htmlfp, "            rotation[i] = dx*up[i]+dy*right[i];\n");
    fprintf(htmlfp, "          }\n");
    fprintf(htmlfp, "          var tmp = 0;\n");
    fprintf(htmlfp, "          for (i = 0; i < 3; i++) {\n");
    fprintf(htmlfp, "            tmp = tmp + rotation[i]*rotation[i];\n");
    fprintf(htmlfp, "          }\n");
    fprintf(htmlfp, "          tmp = Math.sqrt(tmp);\n");
    fprintf(htmlfp, "          for (i = 0; i < 3; i++) {\n");
    fprintf(htmlfp, "            rotation[i] = rotation[i]/tmp;\n");
    fprintf(htmlfp, "          }\n");
    fprintf(htmlfp, "          rotationsMatrix = getRotationMatrix(-Math.sqrt(dx*dx+dy*dy)*0.003, rotation[0], rotation[1], rotation[2])\n");
    fprintf(htmlfp, "          viewMatrix = multMatrix4(rotationsMatrix, viewMatrix);\n");
    fprintf(htmlfp, "          up_dir = [viewMatrix[1], viewMatrix[5], viewMatrix[9]];\n");
    fprintf(htmlfp, "          forward = [viewMatrix[2], viewMatrix[6], viewMatrix[10]];\n");
    fprintf(htmlfp, "          for (i = 0; i < 3; i++) {\n");
    fprintf(htmlfp, "            camera_pos[i] = center_pos[i]+len_forward*forward[i]\n");
    fprintf(htmlfp, "          }\n");
    fprintf(htmlfp, "          drawScene();\n");
    fprintf(htmlfp, "        }\n");
    fprintf(htmlfp, "      }\n");

    
    fprintf(htmlfp, "      viewMatrix = null;\n");
    
    fprintf(htmlfp, "      function calculateViewMatrix() {\n");
    fprintf(htmlfp, "        viewMatrix = [\n");
    fprintf(htmlfp, "          0.0, 0.0, 0.0, 0.0,\n");
    fprintf(htmlfp, "          0.0, 0.0, 0.0, 0.0,\n");
    fprintf(htmlfp, "          0.0, 0.0, 0.0, 0.0,\n");
    fprintf(htmlfp, "          0.0, 0.0, 0.0, 0.0\n");
    fprintf(htmlfp, "        ];\n");
    fprintf(htmlfp, "        var i = 0; var j = 0;\n");
    fprintf(htmlfp, "        var F = [0.0 ,0.0, 0.0];\n");
    fprintf(htmlfp, "        var f = [0.0 ,0.0, 0.0];\n");
    fprintf(htmlfp, "        for (i = 0; i < 3; i++) {\n");
    fprintf(htmlfp, "          F[i] = center_pos[i] - camera_pos[i];\n");
    fprintf(htmlfp, "        }\n");
    fprintf(htmlfp, "        var tmp = 0;\n");
    fprintf(htmlfp, "        for (i = 0; i < 3; i++) {\n");
    fprintf(htmlfp, "          tmp = tmp + F[i]*F[i];\n");
    fprintf(htmlfp, "        }\n");
    fprintf(htmlfp, "        tmp = Math.sqrt(tmp);\n");
    fprintf(htmlfp, "        for (i = 0; i < 3; i++) {\n");
    fprintf(htmlfp, "          f[i] = F[i]/tmp;\n");
    fprintf(htmlfp, "        }\n");
    fprintf(htmlfp, "        var up = [0.0 ,0.0, 0.0];\n");
    fprintf(htmlfp, "        var tmp = 0;\n");
    fprintf(htmlfp, "        for (i = 0; i < 3; i++) {\n");
    fprintf(htmlfp, "          tmp = tmp + up_dir[i]*up_dir[i];\n");
    fprintf(htmlfp, "        }\n");
    fprintf(htmlfp, "        tmp = Math.sqrt(tmp);\n");
    fprintf(htmlfp, "        for (i = 0; i < 3; i++) {\n");
    fprintf(htmlfp, "          up[i] = up_dir[i]/tmp;\n");
    fprintf(htmlfp, "        }\n");
    fprintf(htmlfp, "        var s = [0.0 ,0.0, 0.0];\n");
    fprintf(htmlfp, "        for (i = 0; i < 3; i++) {\n");
    fprintf(htmlfp, "          s[i] = f[(i+1)%%3]*up[(i+2)%%3] - up[(i+1)%%3]*f[(i+2)%%3];\n");
    fprintf(htmlfp, "        }\n");
    fprintf(htmlfp, "        var tmp = 0;\n");
    fprintf(htmlfp, "        for (i = 0; i < 3; i++) {\n");
    fprintf(htmlfp, "          tmp = tmp + s[i]*s[i];\n");
    fprintf(htmlfp, "        }\n");
    fprintf(htmlfp, "        tmp = Math.sqrt(tmp);\n");
    fprintf(htmlfp, "        for (i = 0; i < 3; i++) {\n");
    fprintf(htmlfp, "          s[i] = s[i]/tmp;\n");
    fprintf(htmlfp, "        }\n");
    fprintf(htmlfp, "        var u = [0.0 ,0.0, 0.0];\n");
    fprintf(htmlfp, "        for (i = 0; i < 3; i++) {\n");
    fprintf(htmlfp, "          u[i] = s[(i+1)%%3]*f[(i+2)%%3] - f[(i+1)%%3]*s[(i+2)%%3];\n");
    fprintf(htmlfp, "        }\n");
    fprintf(htmlfp, "        var tmp = 0;\n");
    fprintf(htmlfp, "        for (i = 0; i < 3; i++) {\n");
    fprintf(htmlfp, "          tmp = tmp + u[i]*u[i];\n");
    fprintf(htmlfp, "        }\n");
    fprintf(htmlfp, "        tmp = Math.sqrt(tmp);\n");
    fprintf(htmlfp, "        for (i = 0; i < 3; i++) {\n");
    fprintf(htmlfp, "          u[i] = u[i]/tmp;\n");
    fprintf(htmlfp, "        }\n");
    fprintf(htmlfp, "        for (i = 0; i < 3; i++) {\n");
    fprintf(htmlfp, "          viewMatrix[i+0] = s[i];\n");
    fprintf(htmlfp, "          viewMatrix[i+4] = u[i];\n");
    fprintf(htmlfp, "          viewMatrix[i+8] = -f[i];\n");
    fprintf(htmlfp, "        }\n");
    fprintf(htmlfp, "        viewMatrix[15] = 1;\n");
    fprintf(htmlfp, "        \n");
    fprintf(htmlfp, "        for (i = 0; i < 3; i++) {\n");
    fprintf(htmlfp, "          viewMatrix[3+4*i] = 0;\n");
    fprintf(htmlfp, "          for (j = 0; j < 3; j++) {\n");
    fprintf(htmlfp, "            viewMatrix[3+4*i] = viewMatrix[3+4*i] - viewMatrix[j+4*i]*camera_pos[j];\n");
    fprintf(htmlfp, "          }\n");
    fprintf(htmlfp, "        }\n");
    fprintf(htmlfp, "        viewMatrix = transposeMatrix4(viewMatrix);\n");
    fprintf(htmlfp, "      }\n");
    fprintf(htmlfp, "      var camera_pos = [%f, %f, %f];\n", context_struct_.camera_x, context_struct_.camera_y, context_struct_.camera_z);
    fprintf(htmlfp, "      var center_pos = [%f, %f, %f];\n", context_struct_.center_x, context_struct_.center_y, context_struct_.center_z);
    fprintf(htmlfp, "      var up_dir = [%f, %f, %f];\n", context_struct_.up_x, context_struct_.up_y, context_struct_.up_z);
    fprintf(htmlfp, "      var original_camera_pos = camera_pos.slice(0);\n");
    fprintf(htmlfp, "      var original_center_pos = center_pos.slice(0);\n");
    fprintf(htmlfp, "      var original_up_dir = up_dir.slice(0);\n");
    fprintf(htmlfp, "      function drawScene() {\n");
    fprintf(htmlfp, "        \n");
    fprintf(htmlfp, "        if (!viewMatrix) {\n");
    fprintf(htmlfp, "          calculateViewMatrix();\n");
    fprintf(htmlfp, "        }\n");
    fprintf(htmlfp, "        var verticalFieldOfView = %f;\n", context_struct_.vertical_field_of_view);
    fprintf(htmlfp, "        var zNear = %f;\n", context_struct_.zNear);
    fprintf(htmlfp, "        var zFar = %f;\n", context_struct_.zFar);
    fprintf(htmlfp, "        var aspect = 1.0*gl.viewportWidth/gl.viewportHeight;\n");
    fprintf(htmlfp, "        var f = 1/Math.tan(verticalFieldOfView*Math.PI/360.0);\n");
    fprintf(htmlfp, "        \n");
    fprintf(htmlfp, "        var projectionMatrix = [\n");
    fprintf(htmlfp, "          f/aspect, 0.0, 0.0, 0.0,\n");
    fprintf(htmlfp, "          0.0, f, 0.0, 0.0,\n");
    fprintf(htmlfp, "          0.0, 0.0, (zFar+zNear)/(zNear-zFar), 2*zFar*zNear/(zNear-zFar),\n");
    fprintf(htmlfp, "          0.0, 0.0, -1, 0.0\n");
    fprintf(htmlfp, "        ];\n");
    fprintf(htmlfp, "        projectionMatrix = transposeMatrix4(projectionMatrix);\n");
    fprintf(htmlfp, "        \n");
    fprintf(htmlfp, "        \n");
    fprintf(htmlfp, "        var lightDirection = [\n");
    fprintf(htmlfp, "          %f, %f, %f\n", context_struct_.light_dir[0], context_struct_.light_dir[1], context_struct_.light_dir[2]);
    fprintf(htmlfp, "        ];\n");
    fprintf(htmlfp, "        \n");
    fprintf(htmlfp, "        \n");
    fprintf(htmlfp, "        gl.clearColor(%f,%f,%f,%f);\n", context_struct_.background_color[0], context_struct_.background_color[1], context_struct_.background_color[2], context_struct_.background_color[3]);
    fprintf(htmlfp, "        gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);\n");
    {
        GR3_DrawList_t_ *draw;
        draw = context_struct_.draw_list_;
        while (draw) {
            GLfloat forward[3], up[3], left[3];
            GLfloat model_matrix[4][4] = {{0}};
            float tmp;
            fprintf(htmlfp, "        var meshId = %u;\n", draw->mesh);
            fprintf(htmlfp, "        var mesh = null;\n");
            fprintf(htmlfp, "        for (var meshIndex in meshes) {\n");
            fprintf(htmlfp, "          if (meshes[meshIndex].id == meshId) {\n");
            fprintf(htmlfp, "            mesh = meshes[meshIndex];\n");
            fprintf(htmlfp, "            break;\n");
            fprintf(htmlfp, "          }\n");
            fprintf(htmlfp, "        }\n");
            fprintf(htmlfp, "        mesh.bind()\n");
            fprintf(htmlfp, "        \n");
            fprintf(htmlfp, "        var modelMatrices = new Array();\n");
            fprintf(htmlfp, "        var scales = new Array();\n");
            fprintf(htmlfp, "        var colors = new Array();\n");
            for (i = 0; i < draw->n; i++) {
                {
                    
                    /* Calculate an orthonormal base in IR^3, correcting the up vector 
                     * in case it is not perpendicular to the forward vector. This base
                     * is used to create the model matrix as a base-transformation 
                     * matrix.
                     */
                    /* forward = normalize(&directions[i*3]); */
                    tmp = 0;
                    for (j = 0; j < 3; j++) {
                        tmp+= draw->directions[i*3+j]*draw->directions[i*3+j];
                    }
                    tmp = sqrt(tmp);
                    for (j = 0; j < 3; j++) {
                        forward[j] = draw->directions[i*3+j]/tmp;
                    }/* up = normalize(&ups[i*3]); */
                    tmp = 0;
                    for (j = 0; j < 3; j++) {
                        tmp+= draw->ups[i*3+j]*draw->ups[i*3+j];
                    }
                    tmp = sqrt(tmp);
                    for (j = 0; j < 3; j++) {
                        up[j] = draw->ups[i*3+j]/tmp;
                    }
                    /* left = cross(forward,up); */
                    for (j = 0; j < 3; j++) {
                        left[j] = forward[(j+1)%3]*up[(j+2)%3] - up[(j+1)%3]*forward[(j+2)%3];
                    }
                    /* up = cross(left,forward); */
                    for (j = 0; j < 3; j++) {
                        up[j] = left[(j+1)%3]*forward[(j+2)%3] - forward[(j+1)%3]*left[(j+2)%3];
                    }
                    for (j = 0; j < 3; j++) {
                        model_matrix[0][j] = -left[j];
                        model_matrix[1][j] = up[j];
                        model_matrix[2][j] = forward[j];
                        model_matrix[3][j] = draw->positions[i*3+j];
                    }
                    model_matrix[3][3] = 1;
                }
                
                fprintf(htmlfp, "        var modelMatrix = [\n");
                fprintf(htmlfp, "          %f, %f, %f, %f,\n", model_matrix[0][0], model_matrix[1][0], model_matrix[2][0], model_matrix[3][0]);
                fprintf(htmlfp, "          %f, %f, %f, %f,\n", model_matrix[0][1], model_matrix[1][1], model_matrix[2][1], model_matrix[3][1]);
                fprintf(htmlfp, "          %f, %f, %f, %f,\n", model_matrix[0][2], model_matrix[1][2], model_matrix[2][2], model_matrix[3][2]);
                fprintf(htmlfp, "          %f, %f, %f, %f\n", model_matrix[0][3], model_matrix[1][3], model_matrix[2][3], model_matrix[3][3]);
                fprintf(htmlfp, "        ];\n");
                fprintf(htmlfp, "        modelMatrices.push(transposeMatrix4(modelMatrix));\n");
                
                fprintf(htmlfp, "        var scale = [\n");
                fprintf(htmlfp, "          %f, %f, %f\n", draw->scales[i*3+0], draw->scales[i*3+1], draw->scales[i*3+2]);
                fprintf(htmlfp, "        ];\n");
                fprintf(htmlfp, "        scales.push(scale);\n");
                fprintf(htmlfp, "        \n");
                fprintf(htmlfp, "        var color = [\n");
                fprintf(htmlfp, "          %f, %f, %f\n", draw->colors[i*3+0], draw->colors[i*3+1], draw->colors[i*3+2]);
                fprintf(htmlfp, "        ];\n");
                fprintf(htmlfp, "        colors.push(color);\n");
            }
            fprintf(htmlfp, "        gl.enable(gl.BLEND);\n");
            fprintf(htmlfp, "        gl.blendFunc(gl.CONSTANT_COLOR, gl.ZERO);\n");
            fprintf(htmlfp, "        for (var i = 0; i < %u; i++) {\n", draw->n);
            fprintf(htmlfp, "          gl.blendColor(colors[i][0],colors[i][1],colors[i][2],1.0);\n");
            fprintf(htmlfp, "          mesh.draw(projectionMatrix, viewMatrix, modelMatrices[i], scales[i], lightDirection);\n");
            fprintf(htmlfp, "        }\n");
            draw = draw->next;
        }
    }
    fprintf(htmlfp, "      }\n");
        
    fprintf(htmlfp, "      var shaderProgram;");
    fprintf(htmlfp, "      function initShaderProgram() {\n");
    fprintf(htmlfp, "        var vertexShader = getShader(gl, \"shader-vs\");\n");
    fprintf(htmlfp, "        var fragmentShader = getShader(gl, \"shader-fs\");\n");
    fprintf(htmlfp, "        \n");
    fprintf(htmlfp, "        shaderProgram = gl.createProgram();\n");
    fprintf(htmlfp, "        gl.attachShader(shaderProgram, vertexShader);\n");
    fprintf(htmlfp, "        gl.attachShader(shaderProgram, fragmentShader);\n");    
    fprintf(htmlfp, "        gl.linkProgram(shaderProgram);\n");
    fprintf(htmlfp, "        \n");
    fprintf(htmlfp, "        if (!gl.getProgramParameter(shaderProgram, gl.LINK_STATUS)) {\n");
    fprintf(htmlfp, "          alert(\"Unable to initialize the shader program.\");\n");
    fprintf(htmlfp, "          alert(gl.getProgramInfoLog(shaderProgram));\n");
    fprintf(htmlfp, "        }\n");
    fprintf(htmlfp, "        \n");
    fprintf(htmlfp, "        gl.useProgram(shaderProgram);\n");
    
    fprintf(htmlfp, "        shaderProgram.projectionMatrixLocation = gl.getUniformLocation(shaderProgram, \"ProjectionMatrix\");\n");
    fprintf(htmlfp, "        shaderProgram.viewMatrixLocation = gl.getUniformLocation(shaderProgram, \"ViewMatrix\");\n");
    fprintf(htmlfp, "        shaderProgram.modelMatrixLocation = gl.getUniformLocation(shaderProgram, \"ModelMatrix\");\n");
    fprintf(htmlfp, "        shaderProgram.lightDirectionLocation = gl.getUniformLocation(shaderProgram, \"LightDirection\");\n");
    fprintf(htmlfp, "        shaderProgram.scalesLocation = gl.getUniformLocation(shaderProgram, \"Scales\");\n");
    fprintf(htmlfp, "        \n");
    fprintf(htmlfp, "        shaderProgram.vertexLocation = gl.getAttribLocation(shaderProgram, \"in_Vertex\");\n");
    fprintf(htmlfp, "        shaderProgram.normalLocation = gl.getAttribLocation(shaderProgram, \"in_Normal\");\n");
    fprintf(htmlfp, "        shaderProgram.colorLocation = gl.getAttribLocation(shaderProgram, \"in_Color\");\n");
    fprintf(htmlfp, "        \n");
    fprintf(htmlfp, "        \n");
    fprintf(htmlfp, "      }\n");
    
    fprintf(htmlfp, "      function getShader(gl, id) {\n");
    fprintf(htmlfp, "        var shaderScriptElement = document.getElementById(id);\n");
    fprintf(htmlfp, "        if (!shaderScriptElement) {\n");
    fprintf(htmlfp, "          return null;\n");
    fprintf(htmlfp, "        }\n");
    fprintf(htmlfp, "        \n");
    fprintf(htmlfp, "        var str = \"\";\n");
    fprintf(htmlfp, "        var k = shaderScriptElement.firstChild;\n");
    fprintf(htmlfp, "        while (k) {\n");
    fprintf(htmlfp, "          if (k.nodeType == 3) {\n");
    fprintf(htmlfp, "            str += k.textContent;\n");
    fprintf(htmlfp, "          }\n");
    fprintf(htmlfp, "          k = k.nextSibling;\n");
    fprintf(htmlfp, "        }\n");
    fprintf(htmlfp, "        \n");
    fprintf(htmlfp, "        var shader;\n");
    fprintf(htmlfp, "        if (shaderScriptElement.type == \"x-shader/x-vertex\") {\n");
    fprintf(htmlfp, "          shader = gl.createShader(gl.VERTEX_SHADER);\n");
    fprintf(htmlfp, "        } else if (shaderScriptElement.type == \"x-shader/x-fragment\") {\n");
    fprintf(htmlfp, "          shader = gl.createShader(gl.FRAGMENT_SHADER);\n");
    fprintf(htmlfp, "        } else {\n");
    fprintf(htmlfp, "          return null;\n");
    fprintf(htmlfp, "        }\n");
    fprintf(htmlfp, "        \n");
    fprintf(htmlfp, "        gl.shaderSource(shader, str);\n");
    fprintf(htmlfp, "        gl.compileShader(shader);\n");
    fprintf(htmlfp, "        \n");
    fprintf(htmlfp, "        if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {\n");
    fprintf(htmlfp, "          alert(gl.getShaderInfoLog(shader));\n");
    fprintf(htmlfp, "          return null;\n");
    fprintf(htmlfp, "        }\n");
    fprintf(htmlfp, "        \n");
    fprintf(htmlfp, "        return shader;\n");
    fprintf(htmlfp, "      }\n");
    
    fprintf(htmlfp, "    </script>\n");
    
    fprintf(htmlfp, "    <script id=\"shader-vs\" type=\"x-shader/x-vertex\">\n");
    fprintf(htmlfp, "      uniform mat4 ProjectionMatrix;\n"); 
    fprintf(htmlfp, "      uniform mat4 ViewMatrix;\n"); 
    fprintf(htmlfp, "      uniform mat4 ModelMatrix;\n"); 
    fprintf(htmlfp, "      uniform vec3 LightDirection;\n"); 
    fprintf(htmlfp, "      uniform vec3 Scales;\n"); 
    fprintf(htmlfp, "      attribute vec3 in_Vertex;\n");
    fprintf(htmlfp, "      attribute vec3 in_Normal;\n");
    fprintf(htmlfp, "      attribute vec3 in_Color;\n");
    fprintf(htmlfp, "      varying vec4 Color;\n"); 
    fprintf(htmlfp, "      varying vec3 Normal;\n"); 
    fprintf(htmlfp, "      void main(void) {\n"); 
    fprintf(htmlfp, "        vec4 Position = ViewMatrix*ModelMatrix*(vec4(Scales*in_Vertex,1));\n");
    fprintf(htmlfp, "        gl_Position=ProjectionMatrix*Position;\n"); 
    fprintf(htmlfp, "        Normal = vec3(ViewMatrix*ModelMatrix*vec4(in_Normal,0)).xyz;\n"); 
    fprintf(htmlfp, "        Color = vec4(in_Color,1);\n"); 
    fprintf(htmlfp, "        float diffuse = Normal.z;\n"); 
    fprintf(htmlfp, "        if (dot(LightDirection,LightDirection) > 0.001) {\n");
    fprintf(htmlfp, "          diffuse = dot(normalize(LightDirection),Normal);\n"); 
    fprintf(htmlfp, "        }\n"); 
    fprintf(htmlfp, "        diffuse = abs(diffuse);\n"); 
    fprintf(htmlfp, "        Color.rgb = diffuse*Color.rgb;\n");
    fprintf(htmlfp, "      }\n");
    fprintf(htmlfp, "    </script>\n");
    
    fprintf(htmlfp, "    <script id=\"shader-fs\" type=\"x-shader/x-fragment\">\n");
    fprintf(htmlfp, "      precision mediump float;\n");
    fprintf(htmlfp, "      varying vec4 Color;\n"); 
    fprintf(htmlfp, "      varying vec3 Normal;\n"); 
    fprintf(htmlfp, "      void main(void) {\n"); 
    fprintf(htmlfp, "        gl_FragColor=vec4(Color.rgb,Color.a);\n");
    fprintf(htmlfp, "      }\n");
    fprintf(htmlfp, "    </script>\n");
    
    fprintf(htmlfp, "  </head>\n");
    fprintf(htmlfp, "  <body onload=\"startWebGLCanvas()\">\n");
    fprintf(htmlfp, "    <canvas id=\"webgl-canvas\" width=\"%u\" height=\"%u\" tabindex=\"1\" style=\"outline-style:none;\"></canvas>\n", width, height);
    fprintf(htmlfp, "  </body>\n");
    fprintf(htmlfp, "</html>");
    fclose(htmlfp);
    return GR3_ERROR_NONE;
}

static int gr3_getpovray_(char *pixels, int width, int height, int use_alpha, int ssaa_factor) {
    int i;
#ifdef GR3_USE_WIN
    char *povfile = malloc(40);
    char *pngfile = malloc(40);
    sprintf(povfile,"./gr3.%d.pov",getpid());
    sprintf(pngfile,"./gr3.%d.png",getpid());
#else
    char *povfile = malloc(40);
    char *pngfile = malloc(40);
    sprintf(povfile,"/tmp/gr3.%d.pov",getpid());
    sprintf(pngfile,"/tmp/gr3.%d.png",getpid());
#endif
    gr3_export_pov_(povfile, width, height);
    {
        int res;
        char *povray_call = malloc(strlen(povfile)+strlen(povfile)+80);
#ifdef GR3_USE_WIN
        sprintf(povray_call,"megapov +I%s +O%s +H%d +W%d -D +UA +FN +A +R%d",povfile,pngfile,width,height, ssaa_factor);
#else
        sprintf(povray_call,"povray +I%s +O%s +H%d +W%d -D +UA +FN +A +R%d 2>/dev/null",povfile,pngfile,width,height, ssaa_factor);
#endif
        system(povray_call);
        free(povray_call);
        if (use_alpha) {
            res = gr3_readpngtomemory_((int *)pixels,pngfile,width,height);
            if (res) {
                return GR3_ERROR_EXPORT;
            }
        } else {
            char *raw_pixels = malloc(width*height*4);
            if (!raw_pixels) {
                return GR3_ERROR_OUT_OF_MEM;
            }
            res = gr3_readpngtomemory_((int *)raw_pixels,pngfile,width,height);
            if (res) {
                free(raw_pixels);
                return GR3_ERROR_EXPORT;
            }
            for (i = 0; i < width*height; i++) {
                pixels[3*i+0] = raw_pixels[4*i+0];
                pixels[3*i+1] = raw_pixels[4*i+1];
                pixels[3*i+2] = raw_pixels[4*i+2];
            }
            free(raw_pixels);
        }
        
    }
    remove(povfile);
    remove(pngfile);
    free(povfile);
    free(pngfile);
    return GR3_ERROR_NONE;
}

/*!
 * This function fills a bitmap of the given size (width x height) with the 
 * image created by gr3.
 * \param [in] bitmap       The bitmap that the function has to fill
 * \param [in] width        The width of the bitmap
 * \param [in] height       The height of the bitmap
 * \returns
 * - ::GR3_ERROR_NONE                     on success
 * - ::GR3_ERROR_NOT_INITIALIZED          if gr3 has not been initialized
 * - ::GR3_ERROR_OPENGL_ERR               if an OpenGL error occured
 * - ::GR3_ERROR_CAMERA_NOT_INITIALIZED   if the camera has not been initialized
 * \note The memory bitmap points to must be \f$sizeof(int) \cdot width 
 * \cdot height\f$ bytes in size, so the whole image can be stored.
 */
static int gr3_getpixmap_(char *pixmap, int width, int height, int use_alpha, int ssaa_factor) {
    int x, y;
    int fb_width, fb_height;
    int dx, dy;
    int x_patches, y_patches;
    int view_matrix_all_zeros;
    char *raw_pixels;
    
    GLenum format = use_alpha ? GL_RGBA : GL_RGB;
    int bpp = use_alpha ? 4 : 3;
    GLfloat fovy = context_struct_.vertical_field_of_view;
    GLfloat tan_halffovy = tan(fovy*M_PI/360.0);
    GLfloat aspect = (GLfloat)width/height;
    GLfloat zNear = context_struct_.zNear;
    GLfloat zFar = context_struct_.zFar;
    
    GLfloat right = zNear*tan_halffovy*aspect;
    GLfloat left = -right;
    GLfloat top = zNear*tan_halffovy;
    GLfloat bottom = -top;

    if (context_struct_.is_initialized) {
        if (width == 0 || height == 0 || pixmap == NULL) {
            return GR3_ERROR_INVALID_VALUE;
        }
        view_matrix_all_zeros = 1;
        for (x = 0; x < 4; x++) {
            for (y = 0; y < 4; y++) {
                if (context_struct_.view_matrix[x][y] != 0) {
                    view_matrix_all_zeros = 0;
                }
            }
        }
        if (view_matrix_all_zeros) {
            /* gr3_cameralookat has not been called */
            return GR3_ERROR_CAMERA_NOT_INITIALIZED;
        }
        if (context_struct_.zFar < context_struct_.zNear || 
            context_struct_.zNear <= 0 || 
            context_struct_.vertical_field_of_view >= 180|| 
            context_struct_.vertical_field_of_view <= 0
        ) {
            /* gr3_setcameraprojectionparameters has not been called */
            return GR3_ERROR_CAMERA_NOT_INITIALIZED;
        }
        
        fb_width = context_struct_.init_struct.framebuffer_width;
        fb_height = context_struct_.init_struct.framebuffer_height;
        if (ssaa_factor != 1) {
            raw_pixels = malloc((size_t)fb_width*fb_height*ssaa_factor*ssaa_factor*bpp);
            if (!raw_pixels) {
                return GR3_ERROR_OUT_OF_MEM;
            }
            width = width*ssaa_factor;
            height = height*ssaa_factor;
        }
        
#if GL_ARB_framebuffer_object
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
#else
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, framebuffer);
#endif
        
        x_patches = width/fb_width+(width/fb_width*fb_width < width);
        y_patches = height/fb_height+(height/fb_height*fb_height < height);
        for (y = 0; y < y_patches; y++) {
            for (x = 0; x < x_patches; x++) {
                if ((x+1)*fb_width <= width) {
                    dx = fb_width;
                } else {
                    dx = width - fb_width*x;
                }
                if ((y+1)*fb_height <= height) {
                    dy = fb_height;
                } else {
                    dy = height - fb_height*y;
                }
                {
                    GLfloat projection_matrix[4][4] = {{0}};
                    GLfloat l = left + 1.0f*(right-left)*(x*fb_width)/width;
                    GLfloat r = left + 1.0f*(right-left)*(x*fb_width+dx)/width;
                    GLfloat b = bottom + 1.0f*(top-bottom)*(y*fb_height)/height;
                    GLfloat t = bottom + 1.0f*(top-bottom)*(y*fb_height+dy)/height;
                    
                    /* Source: http://www.opengl.org/sdk/docs/man/xhtml/glFrustum.xml */
                    projection_matrix[0][0] = 2*zNear/(r - l);
                    projection_matrix[2][0] = (r+l)/(r - l);
                    projection_matrix[1][1] = 2*zNear/(t - b);
                    projection_matrix[2][1] = (t+b)/(t - b);
                    projection_matrix[2][2] = (zFar+zNear)/(zNear-zFar);
                    projection_matrix[3][2] = 2*zFar*zNear/(zNear-zFar);
                    projection_matrix[2][3] = -1;
                    
                    context_struct_.projection_matrix = &projection_matrix[0][0];
                    glViewport(0, 0, dx, dy);
                    gr3_draw_(width, height);
                    context_struct_.projection_matrix = NULL;
                }
                glPixelStorei(GL_PACK_ALIGNMENT,1); /* byte-wise alignment */
                if (ssaa_factor == 1) {
                    #ifdef GR3_USE_WIN
                        /* There seems to be a driver error on windows considering 
                           GL_PACK_ROW_LENGTH, so I have to roll my own loop to 
                           read the pixels row-wise instead of copying whole 
                           images. 
                        */
                        {
                            int i;
                            for (i = 0; i < dy; i++)  {
                                glReadPixels(0, i, dx, 1, format, GL_UNSIGNED_BYTE, 
                                             pixmap+bpp*(y*width*fb_height+i*width+x*fb_width));
                            }
                        }
                    #else
                        /* On other systems, GL_PACK_ROW_LENGTH works fine. */
                        glPixelStorei(GL_PACK_ROW_LENGTH,width);
                        glReadPixels(0, 0, dx, dy, format, GL_UNSIGNED_BYTE, 
                                     pixmap+bpp*(y*width*fb_height+x*fb_width));
                    #endif
                } else {
                    #ifdef GR3_USE_WIN
                    {
                        int i;
                        for (i = 0; i < dy; i++)  {
                            glReadPixels(0, i, dx, 1, format, GL_UNSIGNED_BYTE, raw_pixels+i*fb_width);
                        }
                    }
                    #else
                        glPixelStorei(GL_PACK_ROW_LENGTH,fb_width);
                        glReadPixels(0, 0, dx, dy, format, GL_UNSIGNED_BYTE, raw_pixels);
                    #endif
                    {
                        int i,j,k,l,m,v,c;
                        for (i = 0; i < dx/ssaa_factor; i++) {
                            for (j = 0; j < dy/ssaa_factor; j++) {
                                for (l = 0; l < bpp; l++) {
                                    v = 0;
                                    c = 0;
                                    for (k = 0; k < ssaa_factor; k++) {
                                        for (m = 0; m < ssaa_factor; m++) {
                                            if ((ssaa_factor*i+k < dx) && (ssaa_factor*j+m < dy)) {
                                                v += (unsigned char)raw_pixels[bpp*((ssaa_factor*i+k)+(ssaa_factor*j+m)*fb_width) + l];
                                                c++;
                                            }
                                        }
                                    }
                                    v = v/c;
                                    pixmap[bpp*(y*fb_height/ssaa_factor*width/ssaa_factor + x*fb_width/ssaa_factor +i + j*width/ssaa_factor)+l] = v;
                                }
                            }
                        }
                    }
                }
            }
        }
        if (ssaa_factor != 1) {
            free(raw_pixels);
        }
        if (glGetError() == GL_NO_ERROR) {
            return GR3_ERROR_NONE;
        } else {
            return GR3_ERROR_OPENGL_ERR;
        }
    } else {
        return GR3_ERROR_NOT_INITIALIZED;
    }
}

/*!
 * This function is used by other gr3 functions to provide the user with debug
 * information. If the user has set a logging function with 
 * gr3_setlogcallback(), this function will be called. Otherwise logging 
 * messages will be ignored.
 * \param [in] log_message  The logging message
 */
static void gr3_log_(const char *log_message) {
    if (gr3_log_func_) {
        gr3_log_func_(log_message);
    }
}

/*!
 * During software development it will often be helpful to get debug output 
 * from gr3. This information is not printed, but reported directly to the 
 * user by calling a logging callback. This function allows to set this 
 * callback or disable it again by calling it with NULL.
 * \param [in] gr3_log_func The logging callback, a function which gets a const 
 *                          char pointer as its only argument and returns 
 *                          nothing.
 */
GR3API void gr3_setlogcallback(void (*gr3_log_func)(const char *log_message)) {
    gr3_log_func_ = gr3_log_func;
}

/*!
 * This array holds string representations of the different gr3 error codes as 
 * defined in ::int. The elements of this array will be returned by 
 * gr3_geterrorstring. The last element should always be "kEUnknownError" in 
 * case an unkown error code is requested.
 */
static char *error_strings_[] = {
    "GR3_ERROR_NONE",
    "GR3_ERROR_INVALID_VALUE",
    "GR3_ERROR_INVALID_ATTRIBUTE",
    "GR3_ERROR_INIT_FAILED",
    "GR3_ERROR_OPENGL_ERR",
    "GR3_ERROR_OUT_OF_MEM",
    "GR3_ERROR_NOT_INITIALIZED",
    "GR3_ERROR_CAMERA_NOT_INITIALIZED",
    "GR3_ERROR_UNKNOWN"
};

/*!
 * This function returns a string representation of a given error code.
 * \param [in] error    The error code whose represenation will be returned.
 */
GR3API const char *gr3_geterrorstring(int error) {
    int num_errors = sizeof(error_strings_)/sizeof(char *) - 1;
    if (error >= num_errors) error = num_errors;
    return error_strings_[error];
}

/*!
 * This function allows the user to find out how his commands are rendered.
 * \returns If gr3 is initialized, a string in the format:
 *          "gr3 - " + window toolkit + " - " + framebuffer extension + " - " 
 *          + OpenGL version + " - " + OpenGL renderer string
 *          For example "gr3 - GLX - GL_ARB_framebuffer_object - 2.1 Mesa 
 *          7.10.2 - Software Rasterizer" might be returned on a Linux system 
 *          (using GLX) with an available GL_ARB_framebuffer_object 
 *          implementation.
 *          If gr3 is not initialized "Not initialized" is returned.
 */
GR3API const char *gr3_getrenderpathstring(void) {
    return context_struct_.renderpath_string;
}

/*!
 * This function appends a string to the renderpath string returned by 
 * gr3_getrenderpathstring().
 * \param [in] string The string to append
 */
static void gr3_appendtorenderpathstring_(const char *string) {
    char *tmp = malloc(strlen(context_struct_.renderpath_string)+3+strlen(string)+1);
    strcpy(tmp, context_struct_.renderpath_string);
    strcpy(tmp+strlen(context_struct_.renderpath_string), " - ");
    strcpy(tmp+strlen(context_struct_.renderpath_string)+3, string);
    if (context_struct_.renderpath_string != not_initialized_) {
        free(context_struct_.renderpath_string);
    }
    context_struct_.renderpath_string = tmp;
}

#if defined(GR3_USE_WIN)
    /* OpenGL Context creation on windows */
    static HINSTANCE g_hInstance;
    static HWND hWnd;
    static HDC dc;
    static HGLRC glrc;

    BOOL APIENTRY DllMain( HINSTANCE hInstance,
                           DWORD  fdwReason,
                           LPVOID lpvReserved
                         )
    {
        if (fdwReason == DLL_PROCESS_ATTACH) {
            g_hInstance = hInstance;
            /*fprintf(stderr,"DLL attached to a process\n");*/
        }
        return TRUE;
    }

    static int gr3_initGL_WIN_(void) {
        WNDCLASS   wndclass;
        gr3_log_("gr3_initGL_WIN_();");
        glrc = wglGetCurrentContext();
        if (!glrc) {
            /* Register the frame class */ 
            wndclass.style         = 0; 
            wndclass.lpfnWndProc   = DefWindowProc; 
            wndclass.cbClsExtra    = 0; 
            wndclass.cbWndExtra    = 0; 
            wndclass.hInstance     = g_hInstance; 
            wndclass.hIcon         = NULL; 
            wndclass.hCursor       = LoadCursor (NULL,IDC_ARROW); 
            wndclass.hbrBackground = NULL; 
            wndclass.lpszMenuName  = "OpenGLWindow"; 
            wndclass.lpszClassName = "OpenGLWindow"; 
         
            if (RegisterClass(&wndclass))  {
                /*fprintf(stderr,"Window Class registered successfully.\n"); */
            } else {
                return FALSE;
            }
            hWnd = CreateWindow ("OpenGLWindow", 
                     "Generic OpenGL Sample", 
                     0, 
                     0, 
                     0, 
                     1, 
                     1, 
                     NULL, 
                     NULL, 
                     g_hInstance, 
                     NULL); 
            if (hWnd != NULL) {
                /*fprintf(stderr,"Window created successfully.\n"); */
            } else {
                return GR3_ERROR_INIT_FAILED;
            }

            dc = GetDC(hWnd);

            /* Pixel Format selection */ {
                PIXELFORMATDESCRIPTOR pfd;
                int iPixelFormat;
                BOOL result;
                memset(&pfd,0,sizeof(pfd));
                pfd.nSize = sizeof(pfd);
                pfd.nVersion = 1;
                pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL;
                pfd.iPixelType = PFD_TYPE_RGBA;
                pfd.cColorBits = 24;
                pfd.cAlphaBits = 8;
                pfd.cDepthBits = 24;
                pfd.iLayerType = PFD_MAIN_PLANE;
                iPixelFormat = ChoosePixelFormat(dc,&pfd);
                result = SetPixelFormat(dc,iPixelFormat, &pfd);
                if (result) {
                    /*fprintf(stderr,"Pixel Format set for Device Context successfully.\n");*/
                } else {
                    return GR3_ERROR_INIT_FAILED;
                }
            }

            /* OpenGL Rendering Context creation */ {
                BOOL result;
                glrc = wglCreateContext(dc);
                if (glrc != NULL) {
                    /*fprintf(stderr,"OpenGL Rendering Context was created successfully.\n");*/
                } else {
                    return GR3_ERROR_INIT_FAILED;
                }
                result = wglMakeCurrent(dc,glrc);
                if (result) {
                    /*fprintf(stderr,"OpenGL Rendering Context made current successfully.\n");*/
                } else {
                    return GR3_ERROR_INIT_FAILED;
                }
            }
        }
        /* Load Function pointers */ {
            #ifdef GR3_CAN_USE_VBO
                glBufferData = (PFNGLBUFFERDATAPROC)wglGetProcAddress("glBufferData");
                glBindBuffer = (PFNGLBINDBUFFERPROC)wglGetProcAddress("glBindBuffer");
                glGenBuffers = (PFNGLGENBUFFERSPROC)wglGetProcAddress("glGenBuffers");
                glDeleteBuffers = (PFNGLGENBUFFERSPROC)wglGetProcAddress("glDeleteBuffers");
                glVertexAttribPointer = (PFNGLVERTEXATTRIBPOINTERPROC)wglGetProcAddress("glVertexAttribPointer");
                glGetAttribLocation = (PFNGLGETATTRIBLOCATIONPROC)wglGetProcAddress("glGetAttribLocation");
                glEnableVertexAttribArray = (PFNGLENABLEVERTEXATTRIBARRAYPROC)wglGetProcAddress("glEnableVertexAttribArray");
                glUseProgram = (PFNGLUSEPROGRAMPROC)wglGetProcAddress("glUseProgram");
                glDeleteShader = (PFNGLDELETESHADERPROC)wglGetProcAddress("glDeleteShader");
                glLinkProgram = (PFNGLLINKPROGRAMPROC)wglGetProcAddress("glLinkProgram");
                glAttachShader = (PFNGLATTACHSHADERPROC)wglGetProcAddress("glAttachShader");
                glCreateShader = (PFNGLCREATESHADERPROC)wglGetProcAddress("glCreateShader");
                glCompileShader = (PFNGLCOMPILESHADERPROC)wglGetProcAddress("glCompileShader");
                glCreateProgram = (PFNGLCREATEPROGRAMPROC)wglGetProcAddress("glCreateProgram");
                glDeleteProgram = (PFNGLDELETEPROGRAMPROC)wglGetProcAddress("glDeleteProgram");
                glUniform3f = (PFNGLUNIFORM3FPROC)wglGetProcAddress("glUniform3f");
                glUniformMatrix4fv = (PFNGLUNIFORMMATRIX4FVPROC)wglGetProcAddress("glUniformMatrix4fv");
                glUniform4f = (PFNGLUNIFORM4FPROC)wglGetProcAddress("glUniform4f");
                glGetUniformLocation = (PFNGLGETUNIFORMLOCATIONPROC)wglGetProcAddress("glGetUniformLocation");
                glShaderSource = (PFNGLSHADERSOURCEPROC)wglGetProcAddress("glShaderSource");
            #endif
            glDrawBuffers = (PFNGLDRAWBUFFERSPROC)wglGetProcAddress("glDrawBuffers");
            glBlendColor = (PFNGLBLENDCOLORPROC)wglGetProcAddress("glBlendColor");
            #ifdef GL_ARB_framebuffer_object
                glBindRenderbuffer = (PFNGLBINDRENDERBUFFERPROC)wglGetProcAddress("glBindRenderbuffer");
                glCheckFramebufferStatus = (PFNGLCHECKFRAMEBUFFERSTATUSPROC)wglGetProcAddress("glCheckFramebufferStatus");
                glFramebufferRenderbuffer = (PFNGLFRAMEBUFFERRENDERBUFFERPROC)wglGetProcAddress("glFramebufferRenderbuffer");
                glRenderbufferStorage = (PFNGLRENDERBUFFERSTORAGEPROC)wglGetProcAddress("glRenderbufferStorage");
                glBindFramebuffer = (PFNGLBINDFRAMEBUFFERPROC)wglGetProcAddress("glBindFramebuffer");
                glGenFramebuffers = (PFNGLGENFRAMEBUFFERSPROC)wglGetProcAddress("glGenFramebuffers");
                glGenRenderbuffers = (PFNGLGENRENDERBUFFERSPROC)wglGetProcAddress("glGenRenderbuffers");
                glDeleteFramebuffers = (PFNGLDELETEFRAMEBUFFERSPROC)wglGetProcAddress("glDeleteFramebuffers");
                glDeleteRenderbuffers = (PFNGLDELETERENDERBUFFERSPROC)wglGetProcAddress("glDeleteRenderbuffers");
            #endif
            #ifdef GL_EXT_framebuffer_object
                glBindRenderbufferEXT = (PFNGLBINDRENDERBUFFEREXTPROC)wglGetProcAddress("glBindRenderbufferEXT");
                glCheckFramebufferStatusEXT = (PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC)wglGetProcAddress("glCheckFramebufferStatusEXT");
                glFramebufferRenderbufferEXT = (PFNGLFRAMEBUFFERRENDERBUFFEREXTPROC)wglGetProcAddress("glFramebufferRenderbufferEXT");
                glRenderbufferStorageEXT = (PFNGLRENDERBUFFERSTORAGEEXTPROC)wglGetProcAddress("glRenderbufferStorageEXT");
                glBindFramebufferEXT = (PFNGLBINDFRAMEBUFFEREXTPROC)wglGetProcAddress("glBindFramebufferEXT");
                glGenFramebuffersEXT = (PFNGLGENFRAMEBUFFERSEXTPROC)wglGetProcAddress("glGenFramebuffersEXT");
                glGenRenderbuffersEXT = (PFNGLGENRENDERBUFFERSEXTPROC)wglGetProcAddress("glGenRenderbuffersEXT");
                glDeleteFramebuffersEXT = (PFNGLDELETEFRAMEBUFFERSEXTPROC)wglGetProcAddress("glDeleteFramebuffersEXT");
                glDeleteRenderbuffersEXT = (PFNGLDELETERENDERBUFFERSEXTPROC)wglGetProcAddress("glDeleteRenderbuffersEXT");
            #endif
        }
        context_struct_.terminateGL = gr3_terminateGL_WIN_;
        context_struct_.gl_is_initialized = 1;
        gr3_appendtorenderpathstring_("Windows");
        return GR3_ERROR_NONE;
    }
    static void gr3_terminateGL_WIN_(void) {
        gr3_log_("gr3_terminateGL_WIN_();");
        if (dc) {
            wglDeleteContext(glrc);
            ReleaseDC(hWnd,dc);
            DestroyWindow(hWnd);
            UnregisterClass("OpenGLWindow", g_hInstance);
        }
    }
#endif


#if defined(GR3_USE_CGL)
    /* OpenGL Context creation using CGL */
    
    static CGLContextObj cgl_ctx; /*!< A reference to the CGL context */

    /*!
     * This function implements OpenGL context creation using CGL and a Pbuffer.
     * \returns 
     * - ::GR3_ERROR_NONE         on success
     * - ::GR3_ERROR_INIT_FAILED  if an error occurs, additional information might be 
     *                            available via the logging callback.
     */
    static int gr3_initGL_CGL_(void) {
        CGLContextObj ctx;
        CGLPixelFormatObj pix; /* pixel format */
        GLint npix; /* number of virtual screens referenced by pix after 
                       call to CGLChoosePixelFormat*/
        const CGLPixelFormatAttribute pf_attributes[] = {
            kCGLPFAColorSize, 24,
            kCGLPFAAlphaSize, 8,
            kCGLPFADepthSize, 24,
          /*kCGLPFAOffScreen,*//* Using a PBuffer is hardware accelerated, so */
            kCGLPFAPBuffer,    /* we want to use that. */
            0, 0
        };
        gr3_log_("gr3_initGL_CGL_();");
        ctx = CGLGetCurrentContext();
        if (ctx == NULL) {
            CGLChoosePixelFormat(pf_attributes, &pix, &npix);

            CGLCreateContext(pix,NULL,&ctx);
            CGLReleasePixelFormat(pix);

            CGLSetCurrentContext(ctx);
            gr3_appendtorenderpathstring_("CGL");
        } else {
            CGLRetainContext(ctx);
            gr3_appendtorenderpathstring_("CGL (existing context)");
        }
        cgl_ctx = ctx;
        
        context_struct_.terminateGL = gr3_terminateGL_CGL_;
        context_struct_.gl_is_initialized = 1;
        return GR3_ERROR_NONE;
    }

    /*!
     * This function destroys the OpenGL context using CGL.
     */
    static void gr3_terminateGL_CGL_(void) {
        gr3_log_("gr3_terminateGL_CGL_();");
        
        CGLReleaseContext(cgl_ctx);
        context_struct_.gl_is_initialized = 0;
    }
#endif

#if defined(GR3_USE_GLX)
    /* OpenGL Context creation using GLX */

    static Display *display; /*!< The used X display */
    static Pixmap pixmap; /*!< The XPixmap (GLX < 1.4)*/
    static GLXPbuffer pbuffer; /*!< The GLX Pbuffer (GLX >=1.4) */
    static GLXContext context; /*!< The GLX context */

    /*!
     * This function implements OpenGL context creation using GLX 
     * and a Pbuffer if GLX version is 1.4 or higher, or a XPixmap 
     * otherwise.
     * \returns 
     * - ::GR3_ERROR_NONE         on success
     * - ::GR3_ERROR_INIT_FAILED  if initialization failed
     */
    static int gr3_initGL_GLX_(void) {
        int major, minor;
        int fbcount;
        GLXFBConfig *fbc;
        GLXFBConfig fbconfig;
        gr3_log_("gr3_initGL_GLX_();");
        
        display = XOpenDisplay(0);
        if (!display) {
            gr3_log_("Not connected to an X server!");
            return GR3_ERROR_INIT_FAILED;
        }

        context = glXGetCurrentContext();
        if (context != NULL) {
            gr3_appendtorenderpathstring_("GLX (existing context)");
        } else {
            glXQueryVersion(display,&major,&minor);
            if (major > 1 || minor >=4) {
                int fb_attribs[] =
                {
                  GLX_DRAWABLE_TYPE   , GLX_PBUFFER_BIT,
                  GLX_RENDER_TYPE     , GLX_RGBA_BIT,
                  None
                };
                int pbuffer_attribs[] =
                {
                  GLX_PBUFFER_WIDTH   , 1,
                  GLX_PBUFFER_HEIGHT   , 1,
                  None
                };
                gr3_log_("(Pbuffer)");

                fbc = glXChooseFBConfig(display, DefaultScreen(display), fb_attribs, 
                                        &fbcount);
                if (fbcount == 0) {
                    XFree(fbc);
                    XCloseDisplay(display);
                    return GR3_ERROR_INIT_FAILED;
                }
                fbconfig = fbc[0];
                XFree(fbc);
                pbuffer = glXCreatePbuffer(display, fbconfig, pbuffer_attribs);

                context = glXCreateNewContext(display, fbconfig, GLX_RGBA_TYPE, None, True);
                glXMakeContextCurrent(display,pbuffer,pbuffer,context);
                
                context_struct_.terminateGL = gr3_terminateGL_GLX_Pbuffer_;
                context_struct_.gl_is_initialized = 1;
                gr3_appendtorenderpathstring_("GLX (Pbuffer)");
            } else {
                XVisualInfo *visual;
                int fb_attribs[] =
                {
                  GLX_DRAWABLE_TYPE   , GLX_PIXMAP_BIT,
                  GLX_RENDER_TYPE     , GLX_RGBA_BIT,
                  None
                };
                gr3_log_("(XPixmap)");
                fbc = glXChooseFBConfig(display, DefaultScreen(display), fb_attribs, 
                                        &fbcount);
                if (fbcount == 0) {
                    XFree(fbc);
                    XCloseDisplay(display);
                    return GR3_ERROR_INIT_FAILED;
                }
                fbconfig = fbc[0];
                XFree(fbc);

                context = glXCreateNewContext(display, fbconfig, GLX_RGBA_TYPE, None, True);
                visual = glXGetVisualFromFBConfig(display,fbconfig);
                pixmap = XCreatePixmap(display,XRootWindow(display,DefaultScreen(display)),1,1,visual->depth);

                if (glXMakeContextCurrent(display,pixmap,pixmap,context)) {
                    context_struct_.terminateGL = gr3_terminateGL_GLX_Pixmap_;
                    context_struct_.gl_is_initialized = 1;
                    gr3_appendtorenderpathstring_("GLX (XPixmap)");
                } else {
                    glXDestroyContext(display, context);
                    XFreePixmap(display,pixmap);
                    XCloseDisplay(display);
                    return GR3_ERROR_INIT_FAILED;
                }
            }
        }
        /* Load Function pointers */ {
#ifdef GR3_CAN_USE_VBO
            glBufferData = (PFNGLBUFFERDATAPROC)glXGetProcAddress((const GLubyte *)"glBufferData");
            glBindBuffer = (PFNGLBINDBUFFERPROC)glXGetProcAddress((const GLubyte *)"glBindBuffer");
            glGenBuffers = (PFNGLGENBUFFERSPROC)glXGetProcAddress((const GLubyte *)"glGenBuffers");
            glDeleteBuffers = (PFNGLGENBUFFERSPROC)glXGetProcAddress((const GLubyte *)"glDeleteBuffers");
            glVertexAttribPointer = (PFNGLVERTEXATTRIBPOINTERPROC)glXGetProcAddress((const GLubyte *)"glVertexAttribPointer");
            glGetAttribLocation = (PFNGLGETATTRIBLOCATIONPROC)glXGetProcAddress((const GLubyte *)"glGetAttribLocation");
            glEnableVertexAttribArray = (PFNGLENABLEVERTEXATTRIBARRAYPROC)glXGetProcAddress((const GLubyte *)"glEnableVertexAttribArray");
            glUseProgram = (PFNGLUSEPROGRAMPROC)glXGetProcAddress((const GLubyte *)"glUseProgram");
            glDeleteShader = (PFNGLDELETESHADERPROC)glXGetProcAddress((const GLubyte *)"glDeleteShader");
            glLinkProgram = (PFNGLLINKPROGRAMPROC)glXGetProcAddress((const GLubyte *)"glLinkProgram");
            glAttachShader = (PFNGLATTACHSHADERPROC)glXGetProcAddress((const GLubyte *)"glAttachShader");
            glCreateShader = (PFNGLCREATESHADERPROC)glXGetProcAddress((const GLubyte *)"glCreateShader");
            glCompileShader = (PFNGLCOMPILESHADERPROC)glXGetProcAddress((const GLubyte *)"glCompileShader");
            glCreateProgram = (PFNGLCREATEPROGRAMPROC)glXGetProcAddress((const GLubyte *)"glCreateProgram");
            glDeleteProgram = (PFNGLDELETEPROGRAMPROC)glXGetProcAddress((const GLubyte *)"glDeleteProgram");
            glUniform3f = (PFNGLUNIFORM3FPROC)glXGetProcAddress((const GLubyte *)"glUniform3f");
            glUniformMatrix4fv = (PFNGLUNIFORMMATRIX4FVPROC)glXGetProcAddress((const GLubyte *)"glUniformMatrix4fv");
            glUniform4f = (PFNGLUNIFORM4FPROC)glXGetProcAddress((const GLubyte *)"glUniform4f");
            glGetUniformLocation = (PFNGLGETUNIFORMLOCATIONPROC)glXGetProcAddress((const GLubyte *)"glGetUniformLocation");
            glShaderSource = (PFNGLSHADERSOURCEPROC)glXGetProcAddress((const GLubyte *)"glShaderSource");
#endif
            glDrawBuffers = (PFNGLDRAWBUFFERSPROC)glXGetProcAddress((const GLubyte *)"glDrawBuffers");
            /*glBlendColor = (PFNGLBLENDCOLORPROC)glXGetProcAddress((const GLubyte *)"glBlendColor");*/
#ifdef GL_ARB_framebuffer_object
            glBindRenderbuffer = (PFNGLBINDRENDERBUFFERPROC)glXGetProcAddress((const GLubyte *)"glBindRenderbuffer");
            glCheckFramebufferStatus = (PFNGLCHECKFRAMEBUFFERSTATUSPROC)glXGetProcAddress((const GLubyte *)"glCheckFramebufferStatus");
            glFramebufferRenderbuffer = (PFNGLFRAMEBUFFERRENDERBUFFERPROC)glXGetProcAddress((const GLubyte *)"glFramebufferRenderbuffer");
            glRenderbufferStorage = (PFNGLRENDERBUFFERSTORAGEPROC)glXGetProcAddress((const GLubyte *)"glRenderbufferStorage");
            glBindFramebuffer = (PFNGLBINDFRAMEBUFFERPROC)glXGetProcAddress((const GLubyte *)"glBindFramebuffer");
            glGenFramebuffers = (PFNGLGENFRAMEBUFFERSPROC)glXGetProcAddress((const GLubyte *)"glGenFramebuffers");
            glGenRenderbuffers = (PFNGLGENRENDERBUFFERSPROC)glXGetProcAddress((const GLubyte *)"glGenRenderbuffers");
            glDeleteFramebuffers = (PFNGLDELETEFRAMEBUFFERSPROC)glXGetProcAddress((const GLubyte *)"glDeleteFramebuffers");
            glDeleteRenderbuffers = (PFNGLDELETERENDERBUFFERSPROC)glXGetProcAddress((const GLubyte *)"glDeleteRenderbuffers");
#endif
#ifdef GL_EXT_framebuffer_object
            glBindRenderbufferEXT = (PFNGLBINDRENDERBUFFEREXTPROC)glXGetProcAddress((const GLubyte *)"glBindRenderbufferEXT");
            glCheckFramebufferStatusEXT = (PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC)glXGetProcAddress((const GLubyte *)"glCheckFramebufferStatusEXT");
            glFramebufferRenderbufferEXT = (PFNGLFRAMEBUFFERRENDERBUFFEREXTPROC)glXGetProcAddress((const GLubyte *)"glFramebufferRenderbufferEXT");
            glRenderbufferStorageEXT = (PFNGLRENDERBUFFERSTORAGEEXTPROC)glXGetProcAddress((const GLubyte *)"glRenderbufferStorageEXT");
            glBindFramebufferEXT = (PFNGLBINDFRAMEBUFFEREXTPROC)glXGetProcAddress((const GLubyte *)"glBindFramebufferEXT");
            glGenFramebuffersEXT = (PFNGLGENFRAMEBUFFERSEXTPROC)glXGetProcAddress((const GLubyte *)"glGenFramebuffersEXT");
            glGenRenderbuffersEXT = (PFNGLGENRENDERBUFFERSEXTPROC)glXGetProcAddress((const GLubyte *)"glGenRenderbuffersEXT");
            glDeleteFramebuffersEXT = (PFNGLDELETEFRAMEBUFFERSEXTPROC)glXGetProcAddress((const GLubyte *)"glDeleteFramebuffersEXT");
            glDeleteRenderbuffersEXT = (PFNGLDELETERENDERBUFFERSEXTPROC)glXGetProcAddress((const GLubyte *)"glDeleteRenderbuffersEXT");
#endif
        }
        return GR3_ERROR_NONE;
    }

    /*!
     * This function destroys the OpenGL context using GLX with a Pbuffer.
     */
    static void gr3_terminateGL_GLX_Pbuffer_(void) {
        gr3_log_("gr3_terminateGL_GLX_Pbuffer_();");
        
        glXMakeContextCurrent(display,None,None,NULL);
        glXDestroyContext(display, context);
        /*glXDestroyPbuffer(display, pbuffer);*/
        XCloseDisplay(display);
        context_struct_.gl_is_initialized = 0;
    }

    /*!
     * This function destroys the OpenGL context using GLX with a XPixmap.
     */
    static void gr3_terminateGL_GLX_Pixmap_(void) {
        gr3_log_("gr3_terminateGL_GLX_Pixmap_();");
        
        glXMakeContextCurrent(display,None,None,NULL);
        glXDestroyContext(display, context);
        XFreePixmap(display,pixmap);
        XCloseDisplay(display);
        context_struct_.gl_is_initialized = 0;
    }
#endif


/*!
 * The id of the renderbuffer object used for color data.
 */
static GLuint color_renderbuffer = 0;

/*!
 * The id of the renderbuffer object used for depth data.
 */
static GLuint depth_renderbuffer = 0; 

#if GL_ARB_framebuffer_object
    /* Framebuffer Object using OpenGL 3.0 or GL_ARB_framebuffer_object */
    /*!
     * This function implements Framebuffer creation using the 
     * ARB_framebuffer_object extension.
     * \returns
     * - ::GR3_ERROR_NONE          on success
     * - ::GR3_ERROR_OPENGL_ERR    if an OpenGL error occurs
     */
    static int gr3_initFBO_ARB_(void) {
        GLenum framebuffer_status;
        GLenum draw_buffers[] = {GL_COLOR_ATTACHMENT0};
        GLuint _width = context_struct_.init_struct.framebuffer_width;
        GLuint _height = context_struct_.init_struct.framebuffer_height;

        gr3_log_("gr3_initFBO_ARB_();");
         
        glGenFramebuffers(1, &framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        
        glGenRenderbuffers(1, &color_renderbuffer);
        glBindRenderbuffer(GL_RENDERBUFFER, color_renderbuffer);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, _width, _height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color_renderbuffer);
        
        glGenRenderbuffers(2, &depth_renderbuffer);
        glBindRenderbuffer(GL_RENDERBUFFER, depth_renderbuffer);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, _width, _height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_renderbuffer);
        
        glDrawBuffers(1,draw_buffers);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        framebuffer_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (framebuffer_status != GL_FRAMEBUFFER_COMPLETE) {
            return GR3_ERROR_OPENGL_ERR;
        }
        glViewport(0,0,_width,_height);
        glEnable(GL_DEPTH_TEST);
        if (glGetError() != GL_NO_ERROR) {
            gr3_terminateFBO_ARB_();
            return GR3_ERROR_OPENGL_ERR;
        }
        
        context_struct_.terminateFBO = gr3_terminateFBO_ARB_;
        context_struct_.fbo_is_initialized = 1;
        gr3_appendtorenderpathstring_("GL_ARB_framebuffer_object");
        return GR3_ERROR_NONE;
    }

    /*!
     * This function destroys the Framebuffer Object using the 
     * ARB_framebuffer_object extension.
     */
    static void gr3_terminateFBO_ARB_(void) {
        gr3_log_("gr3_terminateFBO_ARB_();");
        
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        
        glDeleteFramebuffers(1, &framebuffer);
        glDeleteRenderbuffers(1, &color_renderbuffer);
        glDeleteRenderbuffers(1, &depth_renderbuffer);
        
        context_struct_.fbo_is_initialized = 0;
    }
#endif


#if GL_EXT_framebuffer_object
    /* Framebuffer Object using GL_EXT_framebuffer_object */
    /*!
     * This function implements Framebuffer creation using the 
     * EXT_framebuffer_object extension.
     * \returns
     * - ::GR3_ERROR_NONE          on success
     * - ::GR3_ERROR_OPENGL_ERR    if an OpenGL error occurs
     */
    static int gr3_initFBO_EXT_(void) {
        GLenum framebuffer_status;
        GLenum draw_buffers[] = {GL_COLOR_ATTACHMENT0_EXT};
        GLuint _width = context_struct_.init_struct.framebuffer_width;
        GLuint _height = context_struct_.init_struct.framebuffer_height;

        gr3_log_("gr3_initFBO_EXT_();");
            
        glGenFramebuffersEXT(1, &framebuffer);
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, framebuffer);
        
        glGenRenderbuffersEXT(1, &color_renderbuffer);
        glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, color_renderbuffer);
        glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_RGBA8, _width, _height);
        glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_RENDERBUFFER_EXT, color_renderbuffer);
        
        glGenRenderbuffersEXT(2, &depth_renderbuffer);
        glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, depth_renderbuffer);
        glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_DEPTH_COMPONENT24, _width, _height);
        glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT, GL_RENDERBUFFER_EXT, depth_renderbuffer);
        
        glDrawBuffers(1,draw_buffers);
        glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
        framebuffer_status = glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT);
        if (framebuffer_status != GL_FRAMEBUFFER_COMPLETE_EXT) {
            return GR3_ERROR_OPENGL_ERR;
        }
        glViewport(0,0,_width,_height);
        glEnable(GL_DEPTH_TEST);
        if (glGetError() != GL_NO_ERROR) {
            gr3_terminateFBO_EXT_();
            return GR3_ERROR_OPENGL_ERR;
        }
        
        context_struct_.terminateFBO = gr3_terminateFBO_EXT_;
        context_struct_.fbo_is_initialized = 1;
        gr3_appendtorenderpathstring_("GL_EXT_framebuffer_object");
        return GR3_ERROR_NONE;
    }

    /*!
     * This function destroys the Framebuffer Object using the 
     * EXT_framebuffer_object extension.
     */
    static void gr3_terminateFBO_EXT_(void) {
        gr3_log_("gr3_terminateFBO_EXT_();");
        
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
        
        glDeleteFramebuffersEXT(1, &framebuffer);
        glDeleteRenderbuffersEXT(1, &color_renderbuffer);
        glDeleteRenderbuffersEXT(1, &depth_renderbuffer);
        
        context_struct_.fbo_is_initialized = 0;
    }
#endif

/*!
 * This function creates the context_struct_.cylinder_mesh for simple drawing
 */
static void gr3_createcylindermesh_(void) {
    int i;
    int n;
    float *vertices;
    float *normals;
    float *colors;
    int num_sides = 36;
    n = 12*num_sides;
    vertices = malloc(n*3*sizeof(float));
    normals = malloc(n*3*sizeof(float));
    colors = malloc(n*3*sizeof(float));
    for (i = 0; i < num_sides; i++) {
        vertices[(12*i+0)*3+0] = cos(M_PI*360/num_sides*i/180);
        vertices[(12*i+0)*3+1] = sin(M_PI*360/num_sides*i/180);
        vertices[(12*i+0)*3+2] = 0;
        vertices[(12*i+1)*3+0] = cos(M_PI*360/num_sides*(i+1)/180);
        vertices[(12*i+1)*3+1] = sin(M_PI*360/num_sides*(i+1)/180);
        vertices[(12*i+1)*3+2] = 0;
        vertices[(12*i+2)*3+0] = cos(M_PI*360/num_sides*i/180);
        vertices[(12*i+2)*3+1] = sin(M_PI*360/num_sides*i/180);
        vertices[(12*i+2)*3+2] = 1;
        
        normals[(12*i+0)*3+0] = cos(M_PI*360/num_sides*i/180);
        normals[(12*i+0)*3+1] = sin(M_PI*360/num_sides*i/180);
        normals[(12*i+0)*3+2] = 0;
        normals[(12*i+1)*3+0] = cos(M_PI*360/num_sides*(i+1)/180);
        normals[(12*i+1)*3+1] = sin(M_PI*360/num_sides*(i+1)/180);
        normals[(12*i+1)*3+2] = 0;
        normals[(12*i+2)*3+0] = cos(M_PI*360/num_sides*i/180);
        normals[(12*i+2)*3+1] = sin(M_PI*360/num_sides*i/180);
        normals[(12*i+2)*3+2] = 0;

        
        vertices[(12*i+3)*3+0] = cos(M_PI*360/num_sides*i/180);
        vertices[(12*i+3)*3+1] = sin(M_PI*360/num_sides*i/180);
        vertices[(12*i+3)*3+2] = 1;
        vertices[(12*i+4)*3+0] = cos(M_PI*360/num_sides*(i+1)/180);
        vertices[(12*i+4)*3+1] = sin(M_PI*360/num_sides*(i+1)/180);
        vertices[(12*i+4)*3+2] = 0;
        vertices[(12*i+5)*3+0] = cos(M_PI*360/num_sides*(i+1)/180);
        vertices[(12*i+5)*3+1] = sin(M_PI*360/num_sides*(i+1)/180);
        vertices[(12*i+5)*3+2] = 1;
        
        normals[(12*i+3)*3+0] = cos(M_PI*360/num_sides*i/180);
        normals[(12*i+3)*3+1] = sin(M_PI*360/num_sides*i/180);
        normals[(12*i+3)*3+2] = 0;
        normals[(12*i+4)*3+0] = cos(M_PI*360/num_sides*(i+1)/180);
        normals[(12*i+4)*3+1] = sin(M_PI*360/num_sides*(i+1)/180);
        normals[(12*i+4)*3+2] = 0;
        normals[(12*i+5)*3+0] = cos(M_PI*360/num_sides*(i+1)/180);
        normals[(12*i+5)*3+1] = sin(M_PI*360/num_sides*(i+1)/180);
        normals[(12*i+5)*3+2] = 0;

        vertices[(12*i+6)*3+0] = cos(M_PI*360/num_sides*i/180);
        vertices[(12*i+6)*3+1] = sin(M_PI*360/num_sides*i/180);
        vertices[(12*i+6)*3+2] = 0;
        vertices[(12*i+7)*3+0] = 0;
        vertices[(12*i+7)*3+1] = 0;
        vertices[(12*i+7)*3+2] = 0;
        vertices[(12*i+8)*3+0] = cos(M_PI*360/num_sides*(i+1)/180);
        vertices[(12*i+8)*3+1] = sin(M_PI*360/num_sides*(i+1)/180);
        vertices[(12*i+8)*3+2] = 0;
        
        normals[(12*i+6)*3+0] = 0;
        normals[(12*i+6)*3+1] = 0;
        normals[(12*i+6)*3+2] = -1;
        normals[(12*i+7)*3+0] = 0;
        normals[(12*i+7)*3+1] = 0;
        normals[(12*i+7)*3+2] = -1;
        normals[(12*i+8)*3+0] = 0;
        normals[(12*i+8)*3+1] = 0;
        normals[(12*i+8)*3+2] = -1;

        vertices[(12*i+9)*3+0] = cos(M_PI*360/num_sides*i/180);
        vertices[(12*i+9)*3+1] = sin(M_PI*360/num_sides*i/180);
        vertices[(12*i+9)*3+2] = 1;
        vertices[(12*i+10)*3+0] = cos(M_PI*360/num_sides*(i+1)/180);
        vertices[(12*i+10)*3+1] = sin(M_PI*360/num_sides*(i+1)/180);
        vertices[(12*i+10)*3+2] = 1;
        vertices[(12*i+11)*3+0] = 0;
        vertices[(12*i+11)*3+1] = 0;
        vertices[(12*i+11)*3+2] = 1;
        
        normals[(12*i+9)*3+0] = 0;
        normals[(12*i+9)*3+1] = 0;
        normals[(12*i+9)*3+2] = 1;
        normals[(12*i+10)*3+0] = 0;
        normals[(12*i+10)*3+1] = 0;
        normals[(12*i+10)*3+2] = 1;
        normals[(12*i+11)*3+0] = 0;
        normals[(12*i+11)*3+1] = 0;
        normals[(12*i+11)*3+2] = 1;
    }
    for (i = 0; i < n*3; i++) {
        colors[i] = 1;
    }
    gr3_createmesh(&context_struct_.cylinder_mesh, n, vertices, normals, colors);
    context_struct_.mesh_list_[context_struct_.cylinder_mesh].data.type = kMTCylinderMesh;
    free(vertices);
    free(normals);
    free(colors);
}

/*!
 * This function allows drawing a cylinder without requiring a mesh.
 * \sa gr3_drawmesh()
 */
GR3API void gr3_drawcylindermesh(int n, const float *positions, const float *directions, const float *colors, const float *radii, const float *lengths) {
    int i;
    int j;
    int min_index;
    int min_n;
    float *scales = malloc(n*3*sizeof(float));
    float *ups = malloc(n*3*sizeof(float));
    for (i = 0; i < n; i++) {
        scales[3*i+0] = radii[i];
        scales[3*i+1] = radii[i];
        scales[3*i+2] = lengths[i];
        min_n = directions[3*i+0];
        min_index = 0;
        for (j = 1; j < 3; j++) {
            if (directions[3*i+j]*directions[3*i+j] < min_n*min_n) {
                min_n = directions[3*i+j];
                min_index = j;
            }
        }
        for (j = 0; j < 3; j++) {
            ups[3*i+j] = 0;
        }
        ups[3*i+min_index] = 1;
    }
    gr3_drawmesh(context_struct_.cylinder_mesh,n,positions,directions,ups,colors,scales);
    free(scales);
    free(ups);
}

/*!
 * This function creates the context_struct_.cone_mesh for simple drawing
 */
static void gr3_createconemesh_(void) {
    int i;
    int n;
    float *vertices;
    float *normals;
    float *colors;

    n = 6*36;
    vertices = malloc(n*3*sizeof(float));
    normals = malloc(n*3*sizeof(float));
    colors = malloc(n*3*sizeof(float));
    for (i = 0; i < 36; i++) {
        vertices[(6*i+0)*3+0] = cos(M_PI*10*i/180);
        vertices[(6*i+0)*3+1] = sin(M_PI*10*i/180);
        vertices[(6*i+0)*3+2] = 0;
        vertices[(6*i+1)*3+0] = cos(M_PI*10*(i+1)/180);
        vertices[(6*i+1)*3+1] = sin(M_PI*10*(i+1)/180);
        vertices[(6*i+1)*3+2] = 0;
        vertices[(6*i+2)*3+0] = 0;
        vertices[(6*i+2)*3+1] = 0;
        vertices[(6*i+2)*3+2] = 1;
        
        normals[(6*i+0)*3+0] = cos(M_PI*10*i/180);
        normals[(6*i+0)*3+1] = sin(M_PI*10*i/180);
        normals[(6*i+0)*3+2] = 0;
        normals[(6*i+1)*3+0] = cos(M_PI*10*(i+1)/180);
        normals[(6*i+1)*3+1] = sin(M_PI*10*(i+1)/180);
        normals[(6*i+1)*3+2] = 0;
        normals[(6*i+2)*3+0] = 0;
        normals[(6*i+2)*3+1] = 0;
        normals[(6*i+2)*3+2] = 1;

        vertices[(6*i+3)*3+0] = cos(M_PI*10*i/180);
        vertices[(6*i+3)*3+1] = sin(M_PI*10*i/180);
        vertices[(6*i+3)*3+2] = 0;
        vertices[(6*i+4)*3+0] = 0;
        vertices[(6*i+4)*3+1] = 0;
        vertices[(6*i+4)*3+2] = 0;
        vertices[(6*i+5)*3+0] = cos(M_PI*10*(i+1)/180);
        vertices[(6*i+5)*3+1] = sin(M_PI*10*(i+1)/180);
        vertices[(6*i+5)*3+2] = 0;
        
        normals[(6*i+3)*3+0] = 0;
        normals[(6*i+3)*3+1] = 0;
        normals[(6*i+3)*3+2] = -1;
        normals[(6*i+4)*3+0] = 0;
        normals[(6*i+4)*3+1] = 0;
        normals[(6*i+4)*3+2] = -1;
        normals[(6*i+5)*3+0] = 0;
        normals[(6*i+5)*3+1] = 0;
        normals[(6*i+5)*3+2] = -1;
    }
    for (i = 0; i < n*3; i++) {
        colors[i] = 1;
    }
    gr3_createmesh(&context_struct_.cone_mesh, n, vertices, normals, colors);
    context_struct_.mesh_list_[context_struct_.cone_mesh].data.type = kMTConeMesh;
    free(vertices);
    free(normals);
    free(colors);
}

/*!
 * This function allows drawing a cylinder without requiring a mesh.
 * \sa gr3_drawmesh()
 */
GR3API void gr3_drawconemesh(int n,const float *positions, const float *directions, const float *colors, const float *radii, const float *lengths) {
    int i;
    int j;
    int min_index;
    int min_n;
    float *scales = malloc(n*3*sizeof(float));
    float *ups = malloc(n*3*sizeof(float));
    for (i = 0; i < n; i++) {
        scales[3*i+0] = radii[i];
        scales[3*i+1] = radii[i];
        scales[3*i+2] = lengths[i];
        min_n = directions[3*i+0];
        min_index = 0;
        for (j = 1; j < 3; j++) {
            if (directions[3*i+j]*directions[3*i+j] < min_n*min_n) {
                min_n = directions[3*i+j];
                min_index = j;
            }
        }
        for (j = 0; j < 3; j++) {
            ups[3*i+j] = 0;
        }
        ups[3*i+min_index] = 1;
        
    }
    gr3_drawmesh(context_struct_.cone_mesh,n,positions,directions,ups,colors,scales);
    free(scales);
    free(ups);
}

/*!
 * This function allows drawing a sphere without requiring a mesh.
 * \sa gr3_drawmesh()
 */
GR3API void gr3_drawspheremesh(int n,const float *positions, const float *colors, const float *radii) {
    int i;
    float *directions = malloc(n*3*sizeof(float));
    float *ups = malloc(n*3*sizeof(float));
    float *scales = malloc(n*3*sizeof(float));
    for (i = 0; i < n; i++) {
        directions[i*3+0] = 0;
        directions[i*3+1] = 0;
        directions[i*3+2] = 1;
        ups[i*3+0] = 0;
        ups[i*3+1] = 1;
        ups[i*3+2] = 0;
        scales[i*3+0] = radii[i];
        scales[i*3+1] = radii[i];
        scales[i*3+2] = radii[i];
    }
    gr3_drawmesh(context_struct_.sphere_mesh,n,positions,directions,ups,colors,scales);
    free(directions);
    free(ups);
    free(scales);
}

/*!
 * This function creates the context_struct_.sphere_mesh for simple drawing
 */
static void gr3_createspheremesh_(void) {
    int i,j;
    int n,iterations = 4;
    float *colors;
    float *vertices_old;
    float *vertices_new;
    float *vertices;
    float *triangle;
    float *triangle_new;
    /* pre-calculated icosahedron vertices */
    float icosahedron[] = {0.52573111211913359, 0, 0.85065080835203988, 0, 0.85065080835203988, 0.52573111211913359, -0.52573111211913359, 0, 0.85065080835203988, 0, 0.85065080835203988, 0.52573111211913359, -0.85065080835203988, 0.52573111211913359, 0, -0.52573111211913359, 0, 0.85065080835203988, 0, 0.85065080835203988, 0.52573111211913359, 0, 0.85065080835203988, -0.52573111211913359, -0.85065080835203988, 0.52573111211913359, 0, 0.85065080835203988, 0.52573111211913359, 0, 0, 0.85065080835203988, -0.52573111211913359, 0, 0.85065080835203988, 0.52573111211913359, 0.52573111211913359, 0, 0.85065080835203988, 0.85065080835203988, 0.52573111211913359, 0, 0, 0.85065080835203988, 0.52573111211913359, 0.52573111211913359, 0, 0.85065080835203988, 0.85065080835203988, -0.52573111211913359, 0, 0.85065080835203988, 0.52573111211913359, 0, 0.85065080835203988, -0.52573111211913359, 0, 0.52573111211913359, 0, -0.85065080835203988, 0.85065080835203988, 0.52573111211913359, 0, 0.85065080835203988, 0.52573111211913359, 0, 0.52573111211913359, 0, -0.85065080835203988, 0, 0.85065080835203988, -0.52573111211913359, 0.52573111211913359, 0, -0.85065080835203988, -0.52573111211913359, 0, -0.85065080835203988, 0, 0.85065080835203988, -0.52573111211913359, 0.52573111211913359, 0, -0.85065080835203988, 0, -0.85065080835203988, -0.52573111211913359, -0.52573111211913359, 0, -0.85065080835203988, 0.52573111211913359, 0, -0.85065080835203988, 0.85065080835203988, -0.52573111211913359, 0, 0, -0.85065080835203988, -0.52573111211913359, 0.85065080835203988, -0.52573111211913359, 0, 0, -0.85065080835203988, 0.52573111211913359, 0, -0.85065080835203988, -0.52573111211913359, 0, -0.85065080835203988, 0.52573111211913359, -0.85065080835203988, -0.52573111211913359, 0, 0, -0.85065080835203988, -0.52573111211913359, 0, -0.85065080835203988, 0.52573111211913359, -0.52573111211913359, 0, 0.85065080835203988, -0.85065080835203988, -0.52573111211913359, 0, 0, -0.85065080835203988, 0.52573111211913359, 0.52573111211913359, 0, 0.85065080835203988, -0.52573111211913359, 0, 0.85065080835203988, 0.85065080835203988, -0.52573111211913359, 0, 0.52573111211913359, 0, 0.85065080835203988, 0, -0.85065080835203988, 0.52573111211913359, -0.85065080835203988, -0.52573111211913359, 0, -0.52573111211913359, 0, 0.85065080835203988, -0.85065080835203988, 0.52573111211913359, 0, -0.52573111211913359, 0, -0.85065080835203988, -0.85065080835203988, -0.52573111211913359, 0, -0.85065080835203988, 0.52573111211913359, 0, 0, 0.85065080835203988, -0.52573111211913359, -0.52573111211913359, 0, -0.85065080835203988, -0.85065080835203988, 0.52573111211913359, 0, -0.85065080835203988, -0.52573111211913359, 0, -0.52573111211913359, 0, -0.85065080835203988, 0, -0.85065080835203988, -0.52573111211913359};
    n = 20;
    vertices_old = malloc(n*3*3*sizeof(float));
    memcpy(vertices_old,icosahedron,n*3*3*sizeof(float));
    for (j = 0; j < iterations; j++) {
        vertices_new = malloc(4*n*3*3*sizeof(float));
        for (i = 0; i < n;i++) {
            float a[3], b[3], c[3];
            float len_a, len_b, len_c;
            triangle = &vertices_old[i*3*3];
            triangle_new = &vertices_new[i*3*3*4];
            a[0] = (triangle[2*3 + 0] + triangle[1*3 + 0])*0.5;
            a[1] = (triangle[2*3 + 1] + triangle[1*3 + 1])*0.5;
            a[2] = (triangle[2*3 + 2] + triangle[1*3 + 2])*0.5;
            len_a = sqrt(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]);
            a[0] = a[0]/len_a;
            a[1] = a[1]/len_a;
            a[2] = a[2]/len_a;
            b[0] = (triangle[0*3 + 0] + triangle[2*3 + 0])*0.5;
            b[1] = (triangle[0*3 + 1] + triangle[2*3 + 1])*0.5;
            b[2] = (triangle[0*3 + 2] + triangle[2*3 + 2])*0.5;
            len_b = sqrt(b[0]*b[0]+b[1]*b[1]+b[2]*b[2]);
            b[0] = b[0]/len_b;
            b[1] = b[1]/len_b;
            b[2] = b[2]/len_b;
            c[0] = (triangle[0*3 + 0] + triangle[1*3 + 0])*0.5;
            c[1] = (triangle[0*3 + 1] + triangle[1*3 + 1])*0.5;
            c[2] = (triangle[0*3 + 2] + triangle[1*3 + 2])*0.5;
            len_c = sqrt(c[0]*c[0]+c[1]*c[1]+c[2]*c[2]);
            c[0] = c[0]/len_c;
            c[1] = c[1]/len_c;
            c[2] = c[2]/len_c;
            
            triangle_new[0*3*3 + 0*3 + 0] = triangle[0*3 + 0];
            triangle_new[0*3*3 + 0*3 + 1] = triangle[0*3 + 1];
            triangle_new[0*3*3 + 0*3 + 2] = triangle[0*3 + 2];
            triangle_new[0*3*3 + 1*3 + 0] = c[0];
            triangle_new[0*3*3 + 1*3 + 1] = c[1];
            triangle_new[0*3*3 + 1*3 + 2] = c[2];
            triangle_new[0*3*3 + 2*3 + 0] = b[0];
            triangle_new[0*3*3 + 2*3 + 1] = b[1];
            triangle_new[0*3*3 + 2*3 + 2] = b[2];
            
            triangle_new[1*3*3 + 0*3 + 0] = a[0];
            triangle_new[1*3*3 + 0*3 + 1] = a[1];
            triangle_new[1*3*3 + 0*3 + 2] = a[2];
            triangle_new[1*3*3 + 1*3 + 0] = b[0];
            triangle_new[1*3*3 + 1*3 + 1] = b[1];
            triangle_new[1*3*3 + 1*3 + 2] = b[2];
            triangle_new[1*3*3 + 2*3 + 0] = c[0];
            triangle_new[1*3*3 + 2*3 + 1] = c[1];
            triangle_new[1*3*3 + 2*3 + 2] = c[2];

            triangle_new[2*3*3 + 0*3 + 0] = triangle[1*3 + 0];
            triangle_new[2*3*3 + 0*3 + 1] = triangle[1*3 + 1];
            triangle_new[2*3*3 + 0*3 + 2] = triangle[1*3 + 2];
            triangle_new[2*3*3 + 1*3 + 0] = a[0];
            triangle_new[2*3*3 + 1*3 + 1] = a[1];
            triangle_new[2*3*3 + 1*3 + 2] = a[2];
            triangle_new[2*3*3 + 2*3 + 0] = c[0];
            triangle_new[2*3*3 + 2*3 + 1] = c[1];
            triangle_new[2*3*3 + 2*3 + 2] = c[2];

            triangle_new[3*3*3 + 0*3 + 0] = a[0];
            triangle_new[3*3*3 + 0*3 + 1] = a[1];
            triangle_new[3*3*3 + 0*3 + 2] = a[2];
            triangle_new[3*3*3 + 1*3 + 0] = triangle[2*3 + 0];
            triangle_new[3*3*3 + 1*3 + 1] = triangle[2*3 + 1];
            triangle_new[3*3*3 + 1*3 + 2] = triangle[2*3 + 2];
            triangle_new[3*3*3 + 2*3 + 0] = b[0];
            triangle_new[3*3*3 + 2*3 + 1] = b[1];
            triangle_new[3*3*3 + 2*3 + 2] = b[2];
        }
        n*=4;
        free(vertices_old);
        vertices_old=vertices_new;
    }
    vertices = vertices_old;
    colors = malloc(n*3*3*sizeof(float));
    for (i = 0; i < n*3*3; i++) {
        colors[i] = 1;
    }
    gr3_createmesh(&context_struct_.sphere_mesh, n*3, vertices, vertices, colors);
    context_struct_.mesh_list_[context_struct_.sphere_mesh].data.type = kMTSphereMesh;
    free(colors);
    free(vertices);
}


static int gr3_readpngtomemory_(int *pixels, const char *pngfile, int width, int height) {
    FILE *png_fp;
    png_structp png_ptr;
    png_infop info_ptr = NULL;
    png_infop end_info = NULL;
    unsigned char sig[8];
    png_bytep *row_pointers;
    int i;
    png_fp = fopen(pngfile,"rb");
    if (!png_fp) {
        return 1;
    }
    fread(sig, 1, 8, png_fp);
    if (!png_check_sig(sig, 8)){
        return 2;
    }
    png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr) {
        return 3;
    }
    info_ptr = png_create_info_struct(png_ptr);
    end_info = png_create_info_struct(png_ptr);
    if (!info_ptr || !end_info) {
        png_destroy_read_struct(&png_ptr, &info_ptr, &end_info);
        return 4;
    }
    
    png_init_io(png_ptr, png_fp);
    
    png_set_sig_bytes(png_ptr, 8);
    png_set_user_limits(png_ptr, width, height);
    png_read_png(png_ptr, info_ptr, PNG_TRANSFORM_IDENTITY, NULL);
    row_pointers = png_get_rows(png_ptr, info_ptr);
    for (i = 0; i < height; i++) {
        memcpy(pixels+(height-1-i)*width,row_pointers[i],width*sizeof(int));
    }
    png_destroy_read_struct(&png_ptr, &info_ptr, &end_info);
    fclose(png_fp);
    return 0;
}