/* Copyright (c) 2014 Scott Kuhl. All rights reserved.
 * License: This code is licensed under a 3-clause BSD license. See
 * the file named "LICENSE" for a full copy of the license.
 */

/** @file
 * @author Scott Kuhl
 */


#include <GL/glew.h>
#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#include <GL/freeglut.h>
#endif


#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <float.h> // for FLT_MAX
#include <libgen.h> // for dirname()
#include <sys/time.h> // gettimeofday()
#include <unistd.h> // usleep()
#include <time.h> // time()
#ifdef __linux__
#include <sys/prctl.h> // kill a forked child when parent exits
#include <signal.h>
#endif

#include "kuhl-nodep.h"

#ifdef KUHL_UTIL_USE_ASSIMP
#include <assimp/cimport.h>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/anim.h>
#endif

#include "kuhl-util.h"
#include "vecmat.h"
#ifdef KUHL_UTIL_USE_IMAGEMAGICK
#include "imageio.h"
#else /* use STB image loading if ImageMagick isn't available' */
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#endif


#ifdef KUHL_UTIL_USE_ASSIMP
/** Create a 4x4 float matrix from an ASSIMP 4x4 matrix structure.
    @param dest The location to store new matrix.
    @param src The location of the original matrix.
*/
void mat4f_from_aiMatrix4x4(float  dest[16], struct aiMatrix4x4 src)
{
	memcpy(dest, &src, sizeof(float)*16);
	mat4f_transpose(dest);
}
#endif


/** Don't call this function, call kuhl_errorcheck() instead. */
int kuhl_errorcheckFileLine(const char *file, int line, const char *func)
{
	GLenum errCode = glGetError();
	if(errCode != GL_NO_ERROR)
	{
		msg(ERROR, "OpenGL error '%s' occurred before %s:%d",
		        gluErrorString(errCode), file, line);
		return 1;
	}
	return 0;
}






/** Creates a vertex of fragment shader from a file. This function
 * loads, compiles, and checks for errors for the shader.
 *
 * @param filename The file containing a GLSL shader.
 *
 * @param shader_type Either GL_FRAGMENT_SHADER or GL_VERTEX_SHADER
 *
 * @return The ID for the shader. Exits if an error occurs.
 */
GLuint kuhl_create_shader(const char *filename, GLuint shader_type)
{
	if((shader_type != GL_FRAGMENT_SHADER &&
	    shader_type != GL_VERTEX_SHADER ) ||
	   filename == NULL)
	{
		fprintf(stderr, "%s: ERROR: You passed inappropriate information into this function.\n", __func__);
		return 0;
	}

	/* Make sure that the shader program functions are available via
	 * an extension or because we are using a new enough version of
	 * OpenGL to be guaranteed that the functions exist. */
	if(shader_type == GL_FRAGMENT_SHADER && !glewIsSupported("GL_ARB_fragment_shader") && !glewIsSupported("GL_VERSION_2_0"))
	{
		msg(FATAL, "glew said fragment shaders are not supported on this machine.\n");
		exit(EXIT_FAILURE);
	}
	if(shader_type == GL_VERTEX_SHADER && !glewIsSupported("GL_ARB_vertex_shader") && !glewIsSupported("GL_VERSION_2_0"))
	{
		msg(FATAL, "glew said vertex shaders are not supported on this machine.\n", __func__);
		exit(EXIT_FAILURE);
	}

	/* read in program from the text file */
	// printf("%s shader: %s\n", shader_type == GL_VERTEX_SHADER ? "vertex" : "fragment" , filename);
	GLuint shader = glCreateShader(shader_type);
	kuhl_errorcheck();
	char *text = kuhl_text_read(filename);
	glShaderSource(shader, 1, (const char**) &text, NULL);
	kuhl_errorcheck();
	free(text);

	/* compile program */
	glCompileShader(shader);

	/* Print log from shader compilation (if there is anything in the log) */
	char logString[1024];
	GLsizei actualLen = 0;
	glGetShaderInfoLog(shader, 1024, &actualLen, logString);
	if(actualLen > 0)
		msg(WARNING, "%s Shader log for %s:\n%s\n", shader_type == GL_VERTEX_SHADER ? "Vertex" : "Fragment", filename, kuhl_trim_whitespace(logString));
	kuhl_errorcheck();

	/* If shader compilation wasn't successful, exit. */
	GLint shaderCompileStatus = GL_FALSE;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &shaderCompileStatus);
	if(shaderCompileStatus == GL_FALSE)
	{
		msg(FATAL, "Failed to compile '%s'\n", filename);
		exit(EXIT_FAILURE);
	}

	return shader;
}


/** Prints out useful information about an OpenGL program including a
 * listing of the active attribute variables and active uniform
 * variables.
 *
 * @param program The program that you want information about.
 */
void kuhl_print_program_info(GLuint program)
{
	/* Attributes */
	GLint numVarsInProg = 0;
	glGetProgramiv(program, GL_ACTIVE_ATTRIBUTES, &numVarsInProg);
	char buf[1024] = "";
	int buflen = 0;
	buflen += snprintf(buf+buflen, 1024-buflen, "GLSL prog %d: Active attributes: ", program);
	for(int i=0; i<numVarsInProg; i++)
	{
		char attribName[1024];
		GLint arraySize = 0;
		GLenum type = 0;
		GLsizei actualLength = 0;

		glGetActiveAttrib(program, i, 1024, &actualLength, &arraySize, &type, attribName);
		GLint location = glGetAttribLocation(program, attribName);
		buflen += snprintf(buf+buflen, 1024-buflen, "%s@%d ", attribName, location);
	}
	if(numVarsInProg == 0)
		printf("[none!]");

	msg(INFO, "%s", buf);
	
	kuhl_errorcheck();
	
	numVarsInProg = 0;
	glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &numVarsInProg);
	buflen = 0;
	buflen += snprintf(buf+buflen, 1024-buflen, "GLSL prog %d: Active uniforms: ", program);
	for(int i=0; i<numVarsInProg; i++)
	{
		char attribName[1024];
		GLint arraySize = 0;
		GLenum type = 0;
		GLsizei actualLength = 0;

		glGetActiveUniform(program, i, 1024, &actualLength, &arraySize, &type, attribName);
		GLint location = glGetUniformLocation(program, attribName);
		buflen += snprintf(buf+buflen, 1024-buflen, "%s@%d ", attribName, location);
	}
	if(numVarsInProg == 0)
		printf("[none!]");

	msg(INFO, "%s", buf);
	
	kuhl_errorcheck();
	
	GLint attachedShaderCount=0;
	GLint binarySize=0;
	GLint deleteStatus=GL_FALSE;
	glGetProgramiv(program, GL_ATTACHED_SHADERS, &attachedShaderCount);
	glGetProgramiv(program, GL_PROGRAM_BINARY_LENGTH, &binarySize);
	glGetProgramiv(program, GL_DELETE_STATUS, &deleteStatus);
	msg(INFO, "GLSL prog %d: AttachedShaderCount=%d Size=%d %s\n",
	       program,
	       attachedShaderCount, binarySize,
	       deleteStatus   == GL_TRUE ? "DELETED!" : "");

	kuhl_errorcheck();
}

/** Detaches shaders from the given GLSL program, deletes the program,
 * and flags the shaders for deletion.
 *
 * @param program The GLSL program to delete
 */
void kuhl_delete_program(GLuint program)
{
	if(!glIsProgram(program))
	{
		msg(WARNING, "Tried to delete a program (%d) that does not exist.", program);
		return;
	}

	GLuint shaders[128];
	GLsizei count = 0;
	glGetAttachedShaders(program, 128, &count, shaders);
	for(int i=0; i<count; i++)
	{
		glDetachShader(program, shaders[i]);
		glDeleteShader(shaders[i]);
	}
	glDeleteProgram(program);
}

/** Creates an OpenGL program from pair of files containing a vertex
 * shader and a fragment shader. This code handles checking for
 * support from the video card, error checking, and setting attribute
 * locations.
 *
 * @param vertexFilename The filename of the vertex program.
 *
 * @param fragFilename The filename of the fragment program.
 *
 * @return If success, returns the GLuint used to refer to the
 * program. Returns 0 if no shader program was created.
 */
GLuint kuhl_create_program(const char *vertexFilename, const char *fragFilename)
{
	if(vertexFilename == NULL || fragFilename == NULL)
	{
		msg(ERROR, "One or more of the parameters were NULL\n");
		return 0;
	}

	/* Create a program to attach our shaders to. */
	GLuint program = glCreateProgram();
	if(program == 0)
	{
		msg(FATAL, "Failed to create program.\n");
		exit(EXIT_FAILURE);
	}
	msg(INFO, "GLSL prog %d: Creating vertex (%s) & fragment (%s) shaders\n",
	    program, vertexFilename, fragFilename);
	
	/* Create the shaders */
	GLuint fragShader   = kuhl_create_shader(fragFilename, GL_FRAGMENT_SHADER);
	GLuint vertexShader = kuhl_create_shader(vertexFilename, GL_VERTEX_SHADER);

	/* Attach shaders, check for errors. */
	glAttachShader(program, fragShader);
	kuhl_errorcheck();
	glAttachShader(program, vertexShader);
	kuhl_errorcheck();

	/* Try to link the program. */
	glLinkProgram(program);
	kuhl_errorcheck();

	/* Check if glLinkProgram was successful. */
	GLint linked;
	glGetProgramiv((GLuint)program, GL_LINK_STATUS, &linked);
	kuhl_errorcheck();

	if(linked == GL_FALSE)
	{
		kuhl_print_program_log(program);
		msg(FATAL, "Failed to link GLSL program.\n");
		exit(EXIT_FAILURE);
	}

	/* We used to call glValidateProgram() here. However, some drivers
	 * assume that you only call glValidateProgram() when you are
	 * ready to draw (i.e., have a vertex array object set up, etc). */
	
	kuhl_print_program_info(program);
    // printf("GLSL program %d: Success!\n", program);
	return program;
}

/** Prints a program log if there is one for an OpenGL program.
 *
 * @param program The OpenGL program that we want to print the log for.
 */
void kuhl_print_program_log(GLuint program)
{
	char logString[1024];
	GLsizei actualLen = 0;
	glGetProgramInfoLog(program, 1024, &actualLen, logString);
	if(actualLen > 0)
		msg(WARNING, "GLSL program log:\n%s\n", logString);
}



static int missingUniformCount = 0; /**< Used by kuhl_get_uniform() */
/** Provides functionality similar to glGetUniformLocation() with
 * error checking. However, unlike glGetUniformLocation(), this
 * function gets the location of the variable from the active OpenGL
 * program instead of a specified one. If a problem occurs, an
 * appropriate error message is printed to the standard error. This
 * function may exit or return -1 if the uniform location is not
 * found.
 *
 * @param uniformName The name of the uniform variable.
 *
 * @return The location of the uniform variable.
 */
GLint kuhl_get_uniform(const char *uniformName)
{
	kuhl_errorcheck();
	if(uniformName == NULL || strlen(uniformName) == 0)
	{
		msg(ERROR, "You asked for the location of an uniform name, but your name was an empty string or a NULL pointer.\n");
		return -1;
	}

	GLint currentProgram = 0;
	glGetIntegerv(GL_CURRENT_PROGRAM, &currentProgram);
	if(currentProgram == 0)
	{
		msg(ERROR, "Can't get the uniform location of %s because no GLSL program is currently being used.\n", uniformName);
		return -1;
	}
	
	if(!glIsProgram(currentProgram))
	{
		msg(ERROR, "The current active program (%d) is not a valid GLSL program.\n", currentProgram);
		return -1;
	}

	GLint loc = glGetUniformLocation(currentProgram, uniformName);
	kuhl_errorcheck();
	if(loc == -1 && missingUniformCount < 50)
	{
		msg(ERROR, "Uniform variable '%s' is missing or inactive in your GLSL program.\n", uniformName);
		missingUniformCount++;
		if(missingUniformCount == 50)
		{
			msg(ERROR, "Hiding any additional error messages.\n");
			msg(ERROR, "Remember that the GLSL variables that do not affect the appearance of your program will be set to inactive by the GLSL compiler\n");
		}
	}
	return loc;
}

/** glGetAttribLocation() with error checking. This function behaves
 * the same as glGetAttribLocation() except that when an error
 * occurs, it prints an error message if the attribute variable doesn't
 * exist (or is inactive) in the GLSL program. glGetAttributeLocation()
 * only returns -1 when the attribute variable is not found.
 *
 * @param program The OpenGL shader program containing the attribute variable.
 *
 * @param attributeName The name of the attribute variable.
 *
 * @return The location of the attribute variable. Returns -1 if the
 * attribute is not found.
 */
GLint kuhl_get_attribute(GLuint program, const char *attributeName)
{
	if(attributeName == NULL || strlen(attributeName) == 0)
	{
		msg(ERROR, "You asked for the location of an attribute name in program %d, but your name was an empty string or a NULL pointer.\n", program);
	}

	if(!glIsProgram(program))
	{
		msg(FATAL, "Cannot get attribute '%s' from program %d because the program is not a valid GLSL program.\n", attributeName, program);
		exit(EXIT_FAILURE);
	}

	int linkStatus;
	glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
	if(linkStatus == GL_FALSE)
	{
		msg(ERROR, "Cannot get attribute '%s' from program %d because the program is not linked.\n", attributeName, program);
	}
	
	GLint loc = glGetAttribLocation(program, attributeName);
	kuhl_errorcheck();
	if(loc == -1)
	{
		msg(ERROR, "Cannot get attribute '%s' from program %d because it is missing or inactive.\n", attributeName, program);
	}
	return loc;
}




static void kuhl_geometry_sanity_check_attribute(GLuint bufferobject, const char *attributeName, GLuint program)
{

	GLint attribLoc = -1;
	if(attributeName != NULL)
		attribLoc = glGetAttribLocation(program, attributeName);

	if(attribLoc != -1) /* If the attribute is actually in the GLSL program */
	{
		/* If some part of this attribute is set in kuhl_geometry... */
		if(attributeName != NULL || bufferobject != 0)
		{
			/* All parts of this attribute should be set */
			if(attributeName == NULL || bufferobject == 0)
			{
				msg(FATAL, "Only part of the attribute was set:"
				    "Name=%s bufferobject=%d\n",
				    attributeName, bufferobject);
				exit(EXIT_FAILURE);
			}
		}
	}
	else /* If the attribute is not in the GLSL program */
	{
		/* In this case, we don't care what information is in
		 * kuhl_geometry for this attribute. However, we do
		 * double-check that we didn't unnecessarily create a
		 * bufferobject for the attribute. */
		if(glIsBuffer(bufferobject))
		{
			msg(ERROR, "We created a buffer object for attribute %s even though it isn't in the GLSL program %d\n", attributeName, program);
			exit(EXIT_FAILURE);
		}
	}
}


/** Checks a kuhl_geometry struct to ensure the values are
 * reasonable. This should be called from kuhl_geometry_init() only to
 * double-check that everything was set up properly. When an error
 * occurs, a message is printed to stderr and exit() is called. After
 * kuhl_geometry_init(), some data such as the content in the arrays
 * and the strings representing the attribute names may no longer be
 * accessible.

 @param geom The kuhl_geometry object to check.
*/
static void kuhl_geometry_sanity_check(kuhl_geometry *geom)
{
	if(geom->program == 0)
	{
		msg(ERROR, "The program element was not set in your kuhl_geometry struct. You must specify which GLSL program will be used with this geometry.\n");
		exit(EXIT_FAILURE);
	}

	/* Check if the program is valid (we don't need to enable it here). */
	if(!glIsProgram(geom->program))
	{
		msg(ERROR, "The program you specified in your kuhl_geometry struct (%d) is not a valid GLSL program.\n", geom->program);
		exit(EXIT_FAILURE);
	}

	/* Try to validate the GLSL program for debugging purposes. */
	glValidateProgram(geom->program);
	kuhl_errorcheck();
	GLint validated;
	glGetProgramiv(geom->program, GL_VALIDATE_STATUS, &validated);
	kuhl_errorcheck();

	if(validated == GL_FALSE)
	{
		kuhl_print_program_log(geom->program);
		msg(ERROR, "Failed to validate GLSL program %d.\n", geom->program);
		exit(EXIT_FAILURE);
	}

	
	if(geom->vertex_count < 1)
	{
		msg(ERROR, "vertex_count must be greater than 0.\n");
		exit(EXIT_FAILURE);
	}

	if(!(geom->primitive_type == GL_POINTS ||
	     geom->primitive_type == GL_LINE_STRIP ||
	     geom->primitive_type == GL_LINE_LOOP ||
	     geom->primitive_type == GL_LINES ||
	     geom->primitive_type == GL_TRIANGLE_STRIP ||
	     geom->primitive_type == GL_TRIANGLE_FAN ||
	     geom->primitive_type == GL_TRIANGLES))
	{
		msg(ERROR, "primitive_type must be set to GL_POINTS, GL_LINE_STRIP, GL_LINE_LOOP, GL_LINES, GL_TRIANGLE_STRIP, GL_TRIANGLE_FAN, or GL_TRIANGLES.\n");
		exit(EXIT_FAILURE);
	}

	for(unsigned int i=0; i<geom->attrib_count; i++)
	{
		kuhl_geometry_sanity_check_attribute(geom->attribs[i].bufferobject,
		                                     geom->attribs[i].name,
		                                     geom->program);
	}
}


/** Applies a transformation matrix to an axis-aligned bounding box to
    produce a new axis aligned bounding box.


    @param bbox The bounding box to rotate (xmin, xmax, ymin, ...)
    @param mat The 4x4 transformation matrix to apply to the bounding box
*/
void kuhl_bbox_transform(float bbox[6], float mat[16])
{
	if(mat == NULL)
		return;

	int xmin=0, xmax=1, ymin=2, ymax=3, zmin=4, zmax=5;

	// The 8 vertices of the bounding box
	float coords[8][3] = { {bbox[xmin], bbox[ymin], bbox[zmin] },
	                       {bbox[xmin], bbox[ymin], bbox[zmax] },
	                       {bbox[xmin], bbox[ymax], bbox[zmin] },
	                       {bbox[xmin], bbox[ymax], bbox[zmax] },
	                       {bbox[xmax], bbox[ymin], bbox[zmax] },
	                       {bbox[xmax], bbox[ymax], bbox[zmin] },
	                       {bbox[xmax], bbox[ymax], bbox[zmax] } };
	// Transform the 8 vertices of the bounding box
	for(int i=0; i<8; i++)
		mat4f_mult_vec4f_new(coords[i], mat, coords[i]);
	
	/* Calculate new axis aligned bounding box */
	for(int i=0; i<6; i=i+2) // set min values to the largest float
		bbox[i] = FLT_MAX;
	for(int i=1; i<6; i=i+2) // set max values to the smallest float
		bbox[i] = -FLT_MAX;
	for(unsigned int i=0; i<8; i++)
	{
		// Check for new min values
		if(coords[i][0] < bbox[0])
			bbox[0] = coords[i][0];
		if(coords[i][1] < bbox[2])
			bbox[2] = coords[i][1];
		if(coords[i][2] < bbox[4])
			bbox[4] = coords[i][2];

		// Check for new max values
		if(coords[i][0] > bbox[1])
			bbox[1] = coords[i][0];
		if(coords[i][1] > bbox[3])
			bbox[3] = coords[i][1];
		if(coords[i][2] > bbox[5])
			bbox[5] = coords[i][2];
	}
}
    

#if 0
/** Checks if the axis-aligned bounding box of two kuhl_geometry objects intersect.

    @return 1 if the bounding boxes intersect; 0 otherwise

    @param geom1 One of the pieces of geometry.
    @param mat1 A 4x4 transformation matrix to be applied to the bounding box of geom1 prior to checking for collision.
    @param geom2 The other piece of geometry.
    @param mat2 A 4x4 transformation matrix to be applied to the bounding box of geom2 prior to checking for collision.
*/
int kuhl_geometry_collide(kuhl_geometry *geom1, float mat1[16],
                          kuhl_geometry *geom2, float mat2[16])
{
	float box1[6], box2[6];
	for(int i=0; i<6; i++)
	{
		box1[i] = geom1->aabbox[i];
		box2[i] = geom2->aabbox[i];
	}
	kuhl_bbox_transform(box1, mat1);
	kuhl_bbox_transform(box2, mat1);

	int xmin=0, xmax=1, ymin=2, ymax=3, zmin=4, zmax=5;
	// If the smallest x coordinate in geom1 is larger than the
	// largest x coordinate in geom2, there is no intersection when we
	// project the bounding boxes onto to the X plane. (geom1 is to
	// the right of geom2). Repeat for Y and Z planes
	if(box1[xmin] > box2[xmax]) return 0;
	if(box1[ymin] > box2[ymax]) return 0;
	if(box1[zmin] > box2[zmax]) return 0;
	// If the largest x coordinate of geom1 is smaller than the
	// largest smallest x coordinate in geom 2, there is no
	// intersection when we project the bounding boxes onto the X
	// plane. (geom1 is to the left of geom2). Repeat for Y and Z
	// planes.
	if(box1[xmax] < box2[xmin]) return 0;
	if(box1[ymax] < box2[ymin]) return 0;
	if(box1[zmax] < box2[zmin]) return 0;
	return 1;
}
#endif


/** Adds a texture to the provided kuhl_geometry object.
 *
 * @param geom The geometry object to add a texture to.
 *
 * @param texture The OpenGL texture ID of the texture.
 *
 * @param name The GLSL variable name that the texture should be connected to.
 *
 * @param kg_options If KG_WARN bit is set, will warn if
 * the GLSL variable is missing from the GLSL program for this piece
 * of geometry. If set to KG_FULL_LIST is set, the texture will
 * be applied to all of the geometry in this list. */
 
void kuhl_geometry_texture(kuhl_geometry *geom, GLuint texture, const char* name, int kg_options)
{
	if(name == NULL || strlen(name) == 0)
	{
		msg(WARNING, "GLSL variable name was NULL or the empty string.\n");
		return;
	}
	if(geom == NULL)
	{
		msg(WARNING, "Geometry struct is null while trying to set texture %s.\n", name);
		return;
	}
	if(texture == 0)
	{
		msg(WARNING, "Texture was set to 0 while trying to set texture %s\n", name);
		return;
	}
	if(!glIsTexture(texture))
	{
		msg(WARNING, "You tried to set the texture to an invalid texture %d (detected while trying to set texture %s)\n", texture, name);
		return;
	}

	if(kg_options & KG_FULL_LIST && geom->next != NULL)
		kuhl_geometry_texture(geom->next, texture, name, kg_options);
	
	if(!glIsVertexArray(geom->vao))
	{
		msg(WARNING, "This geometry object has an invalid vertex array object %d (detected while setting texture %s)\n", geom->vao, name);
		return;
	}
	
	/* If this attribute isn't available in the GLSL program, move
	 * on to the next one. */
	GLint samplerLocation = glGetUniformLocation(geom->program, name);
	if(samplerLocation == -1)
	{
		if(kg_options & KG_WARN)
			msg(WARNING, "Texture sampler '%s' was missing in GLSL program %d.\n", name, geom->program);
		return;
	}

	/* If another attribute in kuhl_geometry has the same name,
	 * overwrite it. */
	unsigned int destIndex = geom->texture_count;
	for(unsigned int i=0; i<geom->texture_count; i++)
	{
		if(strcmp(name, geom->textures[i].name) == 0)
			destIndex = i;
	}
	/* If overwriting, free resources from old attribute. */
	if(destIndex < geom->texture_count)
	{
		free(geom->textures[destIndex].name);
		/* Don't free texture since multiple kuhl_geometry objects may
		 * be using the same texture. */
	}
	/* Increment attribute count if necessary. */
	if(destIndex == geom->texture_count)
		geom->texture_count++;
	
	/* If we are writing past the end of the array. */
	if(destIndex == MAX_TEXTURES)
	{
		msg(ERROR, "You tried to add more than %d textures to a kuhl_geometry object\n", MAX_TEXTURES);
		exit(EXIT_FAILURE);
	}

	geom->textures[destIndex].name = strdup(name);
	geom->textures[destIndex].textureId = texture;
}



/** Finds the index of a kuhl_attrib stored inside of a kuhl_geometry
 * by GLSL variable name.
 *
 * @param geom The geometry object to search.
 *
 * @param name The GLSL variable name of the attribute that you are
 * looking for.
 *
 * @return The index into geom->attribs[] array of the
 * attribute. Returns -1 if the attribute was not found.
 */
int kuhl_geometry_attrib_index(kuhl_geometry *geom, const char *name)
{
	if(geom == NULL || name == NULL)
		return -1;
	for(unsigned int i=0; i<geom->attrib_count; i++)
	{
		if(strcmp(name, geom->attribs[i].name) == 0)
			return (int) i;
	}
	return -1;
}

/** Retrieves vertex attribute information stored in an OpenGL array
 * buffer.
 *
 * @param geom The geometry object containing the attribute that you
 * want to retrieve.
 *
 * @param name The GLSL variable name of the attribute that you are
 * interested in.
 *
 * @param size A pointer to an integer that will be filled in with the
 * length of the data retrieved.
 *
 * @return A pointer to a array of floats which contains all of
 * per-vertex data for this attribute. Any changes you make to the
 * array will automatically be propagated back to OpenGL before the
 * next time the geometry is drawn. The array should NOT be
 * free()'d. It also should NOT be accessed after the geometry is
 * drawn. If you want to continue to access the data, you should call
 * kuhl_geometry_attrib_get() every frame. If you aren't changing the
 * data but still want access to it, it is best to make a copy of the
 * array that kuhl_geometry_attrib_get() returns instead of calling it
 * every single frame to retrieve the same data repeatedly.
 */
GLfloat* kuhl_geometry_attrib_get(kuhl_geometry *geom, const char *name, GLint *size)
{
	if(size != NULL)
		*size = 0;

	if(geom == NULL || name == NULL || size == NULL)
		return NULL;

	int index = kuhl_geometry_attrib_index(geom, name);
	if(index < 0)
		return NULL;

	/* Bind the VAO and the buffer we are interested in */
	kuhl_attrib *attrib = &(geom->attribs[index]);
	if(!glIsBuffer(attrib->bufferobject) || !glIsVertexArray(geom->vao))
		return NULL;
	glBindVertexArray(geom->vao);
	glBindBuffer(GL_ARRAY_BUFFER, attrib->bufferobject);
	kuhl_errorcheck();

	/* Get the size of the buffer */
	GLint bufferSize = 0;
	glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &bufferSize);
	GLint bufferNumFloats = bufferSize / sizeof(GLfloat);

	/* Get a pointer to the memory-mapped array (but first check if
	 * the buffer is already mapped. */
	GLfloat *ret;
	glGetBufferPointerv(GL_ARRAY_BUFFER, GL_BUFFER_MAP_POINTER, (void**) &ret);
	if(ret == NULL) /* If buffer is not already mapped */
		ret = (GLfloat*) glMapBuffer(GL_ARRAY_BUFFER, GL_READ_WRITE);

	/* NOTE: We will unmap any buffer that needs unmapping in
	 * kuhl_geometry_draw() before we draw. */
	kuhl_errorcheck();
	if(ret == NULL)
		return NULL;
	*size = bufferNumFloats;

	// unbind
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);
	kuhl_errorcheck();

	return ret;
}

/** Changes the GLSL program that is used by a kuhl_geometry object.
 *
 * @param geom A geometry that you want to change the GLSL program for.
 *
 * @param program The new GLSL program you wish to use with the geometry.
 *
 * @param kg_options Set this to KG_FULL_LIST to apply the new program
 * to all geometries in the kuhl_geometry linked list. Otherwise, set
 * to 0.
 */
void kuhl_geometry_program(kuhl_geometry *geom, GLuint program, int kg_options)
{
	if(geom == NULL)
		return;

	if(kg_options & KG_FULL_LIST)
		kuhl_geometry_program(geom->next, program, kg_options);

	// If the caller deleted their old program and created a new one,
	// the new program ID might match the old program ID. Therefore,
	// we can't rely on the program IDs to verify that the user
	// changed the program.

	if(!glIsProgram(program))
	{
		msg(WARNING, "GLSL program %d is not a valid program.\n",program);
	}
	
	geom->program = program;
	
	glBindVertexArray(geom->vao);
	for(unsigned int i=0; i<geom->attrib_count; i++)
	{
		kuhl_attrib *attrib = &(geom->attribs[i]);
		glBindBuffer(GL_ARRAY_BUFFER, attrib->bufferobject);
		kuhl_errorcheck();

		GLint attribLocation = kuhl_get_attribute(geom->program, attrib->name);
		glEnableVertexAttribArray(attribLocation);

		/* Get the size of the buffer */
		GLint bufferSize = 0;
		glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &bufferSize);
		GLint bufferNumFloats = bufferSize/sizeof(GLfloat);
		GLint components = bufferNumFloats / geom->vertex_count;

		/* Connect this vertex attribute with the (possibly different)
		 * attribute location. */
		glVertexAttribPointer(
			attribLocation, // attribute location in glsl program
			components, // number of elements (x,y,z)
			GL_FLOAT, // type of each element
			GL_FALSE, // should OpenGL normalize values?
			0,        // no extra data between each position
			0 );      // offset of first element
		kuhl_errorcheck();
	}

	/* NOTE: We do not have to update the uniform locations because
	 * they are determined in kuhl_geometry_draw() */
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);
}


/** Adds a vertex attribute (such as vertex position, normal, color,
 * texture coordinate, etc) to the geometry object.
 *
 * @param geom The geometry to add the attribute to.
 *
 * @param data An array of floats that contains the attribute
 * data. This array should contain geom->vertex_count * components
 * floats.
 *
 * @param components The number of floats per vertex in this attribute.
 *
 * @param name The GLSL variable name that this attribute should be
 * connected to.
 *
 * @param warnIfAttribMissing If nonzero, print a warning if the
 * attribute isn't present in the GLSL program for this geometry
 * object.
 */
void kuhl_geometry_attrib(kuhl_geometry *geom, const GLfloat *data, GLuint components, const char* name, int warnIfAttribMissing)
{
	if(name == NULL || strlen(name) == 0)
	{
		msg(WARNING, "Unable to add an attribute that is NULL or an empty string.\n");
		return;
	}
	if(geom == NULL)
	{
		msg(WARNING, "Unable to add attribute '%s' to the geometry object because you passed in a geometry object that was set to NULL.\n",name);
		return;
	}
	if(data == NULL)
	{
		msg(WARNING, "Unable to add attribute '%s' to the geometry object because you passed in an array set to NULL.\n",
		             name);
		return;
	}
	if(components == 0)
	{
		msg(WARNING, "Unable to add attribute '%s' to the geometry object because this attribute has 0 components.\n",
		             name);
		return;
	}
	if(!glIsVertexArray(geom->vao))
	{
		msg(WARNING, "Unable to add attribute '%s' to the geometry object because the geometry has an invalid vertex array object %d\n", geom->vao, name);
		return;
	}

	/* If this attribute isn't available in the GLSL program, move
	 * on to the next one. */
	// GLint attribLocation = kuhl_get_attribute(geom->program, name);

	// Get attribute location directly so kuhl_get_attribute() and
	// this function don't print the same error repeatedly.
	GLint attribLocation = glGetAttribLocation(geom->program, name);
	if(attribLocation == -1)
	{
		if(warnIfAttribMissing)
			msg(WARNING, "Unable to add attribute '%s' to the geometry object because it was missing or inactive in program %d\n",
			    name, geom->program);
		return;
	}

	/* If another attribute in kuhl_geometry has the same name,
	 * overwrite it. */
	int destIndex = kuhl_geometry_attrib_index(geom, name);

	if(destIndex < 0)
	{
		/* If this is a new attribute for this geometry object */
		destIndex = geom->attrib_count;
		geom->attrib_count++;
	}
	else
	{
		/* If overwriting, free resources from old attribute. */
		free(geom->attribs[destIndex].name);
		if(glIsBuffer(geom->attribs[destIndex].bufferobject))
			glDeleteBuffers(1, &(geom->attribs[destIndex].bufferobject));
	}
//	printf("%s: Storing attribute %s at index %d in kuhl_geometry; connected to location %d in program %d\n", __func__, name, destIndex, attribLocation, geom->program);
	
	/* If we are writing past the end of the array. */
	if(destIndex == MAX_ATTRIBUTES)
	{
		msg(FATAL, "You tried to add more than %d attributes to a kuhl_geometry object\n", MAX_ATTRIBUTES);
		exit(EXIT_FAILURE);
	}

	/* Set up this attribute. */
	kuhl_attrib *attrib = &(geom->attribs[destIndex]);
	attrib->name = strdup(name);

	/* Switch to our vertex array object. */
	glBindVertexArray(geom->vao);

	/* Enable this attribute location for this vertex array object. */
	glEnableVertexAttribArray(attribLocation);
	
	/* Ask OpenGL for one new buffer "name" (or ID number). */
	glGenBuffers(1, &(attrib->bufferobject));
	/* Tell OpenGL that we are going to use this buffer until we
	 * say otherwise. GL_ARRAY_BUFFER basically means that the
	 * data stored in this buffer will be an array containing
	 * vertex information. */
	glBindBuffer(GL_ARRAY_BUFFER, attrib->bufferobject);
	kuhl_errorcheck();

	/* Copy our data into the buffer object that is currently bound. */
	glBufferData(GL_ARRAY_BUFFER,
	             sizeof(GLfloat)*geom->vertex_count*components,
	             data, GL_STATIC_DRAW);
	kuhl_errorcheck();

	/* Tell OpenGL some information about the data that is in the
	 * buffer. Among other things, we need to tell OpenGL which
	 * attribute number (i.e., variable) the data should correspond to
	 * in the vertex program. */
	glVertexAttribPointer(
		attribLocation, // attribute location in glsl program
		components, // number of elements (x,y,z)
		GL_FLOAT, // type of each element
		GL_FALSE, // should OpenGL normalize values?
		0,        // no extra data between each position
		0 );      // offset of first element
	kuhl_errorcheck();

	// unbind
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);
}

/** Calculates the number of objects in the kuhl_geometry linked list.

    @param geom The geometry object which you want to know the length of.

    @return The number of geometry objects in the list.
*/
unsigned int kuhl_geometry_count(const kuhl_geometry *geom)
{
	if(geom == NULL)
		return 0;
	const kuhl_geometry *g = geom;
	unsigned int count = 1;
	while(g->next != NULL)
	{
		count++;
		g = g->next;
	}
	return count;
}


/** Initializes the vertex array object (VAO) associated with this
    kuhl_geometry and initializes other variables inside of the struct.

    @param geom A kuhl_geometry struct populated with the information
    necessary to draw some geometry.

    @param program A GLSL program to be used when rendering this
    geometry.

    @param vertexCount The number of vertices in this piece of geometry.

    @param primitive_type OpenGL primitive type of the geometry.
*/
void kuhl_geometry_new(kuhl_geometry *geom, GLuint program, unsigned int vertexCount, GLint primitive_type)
{
	kuhl_errorcheck();

	/* Ask OpenGL for one vertex array object "name" (really an
	 * integer that you can think of as an ID number) that we can use
	 * for a new VAO (vertex array object) */
	glGenVertexArrays(1, &(geom->vao));
	/* Bind to the VAO to finish creating it */
	glBindVertexArray(geom->vao);
	glBindVertexArray(0); // unbind

	/* Check if the program is valid (we don't need to enable it here). */
	if(!glIsProgram(program))
	{
		msg(ERROR, "The program you specified in your kuhl_geometry struct (%d) is not a valid GLSL program.\n", geom->program);
		exit(EXIT_FAILURE);
	}

	if(vertexCount == 0)
	{
		msg(WARNING, "You are creating a geometry object with a vertexCount of 0.\n");
	}

	geom->program = program;
	geom->vertex_count = vertexCount;
	geom->primitive_type = primitive_type;

	geom->attrib_count = 0;
	geom->texture_count = 0;

	geom->indices = NULL;
	geom->indices_len = 0;
	geom->indices_bufferobject = 0;

	mat4f_identity(geom->matrix);
	geom->has_been_drawn = 0;
	
#if KUHL_UTIL_USE_ASSIMP
	geom->assimp_node  = NULL;
	geom->assimp_scene = NULL;
	geom->bones        = NULL;
#endif

	geom->next = NULL;
}

/** Applies a set of indices to the geometry so that vertices can be
 * re-used by multiple triangles or lines.
 *
 * @param geom The geometry that the indices should be used with.
 *
 * @param indicies A list of indices. Each index refers to a specific vertex.
 *
 * @param indexCount The number of indices. For example, if the
 * geometry object consists of triangles, indexCount should be
 * numTriangles*3.
*/
void kuhl_geometry_indices(kuhl_geometry *geom, GLuint *indices, GLuint indexCount)
{
	if(indexCount == 0 || indices == NULL)
	{
		printf("%s: WARNING: indexCount was zero or indices array was NULL\n", __func__);
		return;
	}

	if(geom->primitive_type == GL_TRIANGLES && indexCount % 3 != 0)
	{
		printf("%s: ERROR: indexCount=%u was not a multiple of 3 even though this geometry has triangles in it.", __func__, indexCount);
		exit(EXIT_FAILURE);
	}
	else if(geom->primitive_type == GL_LINES && indexCount % 2 != 0)
	{
		printf("%s: ERROR: indexCount=%u was not a multiple of 2 even though this geometry has lines in it.", __func__, indexCount);
		exit(EXIT_FAILURE);
	}
	
	geom->indices_len = indexCount;
	geom->indices     = indices;

	/* Verify that the indices the user passed in are
	 * appropriate. If there are only 10 vertices, then a user
	 * can't draw a vertex at index 10, 11, 13, etc. */
	for(GLuint i=0; i<geom->indices_len; i++)
	{
		if(geom->indices[i] >= geom->vertex_count)
			fprintf(stderr, "%s: kuhl_geometry has %d vertices but indices[%d] is asking for vertex at index %d to be drawn.\n", __func__, geom->vertex_count, i, geom->indices[i]);
	}

	/* Enable VAO */
	glBindVertexArray(geom->vao);
		
	/* Set up a buffer object (BO) which is a place to store the
	 * *indices* on the graphics card. */
	glGenBuffers(1, &(geom->indices_bufferobject));
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, geom->indices_bufferobject);
	kuhl_errorcheck();

	/* Copy the indices data into the currently bound buffer. */
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(GLuint)*geom->indices_len,
	             geom->indices, GL_STATIC_DRAW);
	kuhl_errorcheck();
	// Don't unbind GL_ELEMENT_ARRAY_BUFFER since the VAO keeps track of this for us.

	// unbind vao
	glBindVertexArray(0);
}



#if 0
void kuhl_geometry_init(kuhl_geometry *geom)
{

	/* Calculate the bounding box. */
	for(int i=0; i<6; i=i+2) // set min values to the largest float
		geom->aabbox[i] = FLT_MAX;
	for(int i=1; i<6; i=i+2) // set max values to the smallest float
		geom->aabbox[i] = -FLT_MAX;
	for(unsigned int i=0; i<geom->vertex_count; i++)
	{
		// Check for new min values
		if(geom->attrib_pos[i*3+0] < geom->aabbox[0])
			geom->aabbox[0] = geom->attrib_pos[i*3+0];
		if(geom->attrib_pos[i*3+1] < geom->aabbox[2])
			geom->aabbox[2] = geom->attrib_pos[i*3+1];
		if(geom->attrib_pos[i*3+2] < geom->aabbox[4])
			geom->aabbox[4] = geom->attrib_pos[i*3+2];

		// Check for new max values
		if(geom->attrib_pos[i*3+0] > geom->aabbox[1])
			geom->aabbox[1] = geom->attrib_pos[i*3+0];
		if(geom->attrib_pos[i*3+1] > geom->aabbox[3])
			geom->aabbox[3] = geom->attrib_pos[i*3+1];
		if(geom->attrib_pos[i*3+2] > geom->aabbox[5])
			geom->aabbox[5] = geom->attrib_pos[i*3+2];
	}

}
#endif

/** Draws a kuhl_geometry struct to the screen. The struct passed into
 * this function should have been set up with kuhl_geometry_new() and
 * at least one position attribute with kuhl_geometry_attrib() before
 * calling this function.

 @param geom The geometry to draw to the screen. If the kuhl_geometry
 object is a part of a linked list, this function will draw each of
 the objects in order. */
void kuhl_geometry_draw(kuhl_geometry *geom)
{
	if(geom == NULL)
		return;
	
	kuhl_errorcheck();
	
	/* Record the OpenGL state so that we can restore it when we have
	 * finished drawing. */
	GLint previouslyUsedProgram = 0;
	glGetIntegerv(GL_CURRENT_PROGRAM, &previouslyUsedProgram);
	GLint previouslyBoundTexture = 0;
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &previouslyBoundTexture);
	GLint previouslyActiveTexture = 0;
	glGetIntegerv(GL_ACTIVE_TEXTURE, &previouslyActiveTexture);
	GLint previousVAO=0;
	glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVAO);

	/* Check that there is a valid program and VAO object for us to use. */
	if(glIsProgram(geom->program) == 0 || glIsVertexArray(geom->vao) == 0)
	{
		fprintf(stderr, "%s: Program (%d) or vertex array object (%d) were invalid\n",
		        __func__, geom->program, geom->vao);
		kuhl_errorcheck();
		return;
	}

	glUseProgram(geom->program);
	kuhl_errorcheck();

	/* Bind all of the textures used in this geometry to texture
	 * units. */
	int hasTex = 0;
	for(unsigned int i=0; i<geom->texture_count; i++)
	{
		kuhl_texture *tex = &(geom->textures[i]);
		if(!glIsTexture(tex->textureId))
			continue;

		/* Check if the sampler variable is available in the GLSL
		 * program. If not, don't send the texture. */
		GLint loc = glGetUniformLocation(geom->program, tex->name);
		if(loc == -1)
			continue;

		if(strcmp(tex->name, "tex") == 0)
			hasTex = 1;
		
		/* Tell OpenGL that the texture that we refer to in our
		 * GLSL program is going to be in texture unit number 'i'.
		 */
		glUniform1i(loc, i);
		kuhl_errorcheck();
		/* Turn on appropriate texture unit */
		glActiveTexture(GL_TEXTURE0+i);
		kuhl_errorcheck();
		/* Bind the texture that we want to use while the correct
		 * texture unit is enabled. */
		glBindTexture(GL_TEXTURE_2D, tex->textureId);
		kuhl_errorcheck();
	}

	GLint loc;
	loc = glGetUniformLocation(geom->program, "HasTex");
	if(loc != -1)
	    glUniform1i(loc, hasTex);

	/* Try to set uniform variables if they are active in the current
	 * GLSL program. If they are not active, don't print any warning
	 * messages. */
	int numBones = 0;
#ifdef KUHL_UTIL_USE_ASSIMP
	loc = glGetUniformLocation(geom->program, "BoneMat");
	if(loc != -1 && geom->bones)
	{
		glUniformMatrix4fv(loc, MAX_BONES, 0, geom->bones->matrices[0]);
		numBones = geom->bones->count;
	}
#endif
	loc = glGetUniformLocation(geom->program, "NumBones");
	if(loc != -1)
	    glUniform1i(loc, numBones);

	loc = glGetUniformLocation(geom->program, "GeomTransform");
	if(loc != -1)
		glUniformMatrix4fv(loc, 1, 0, geom->matrix);
	else
	{ /* If the geom->matrix was not the identity and if it is not in
	   * the GLSL shader program, print a helpful warning message. */
		float identity[16];
		mat4f_identity(identity);
		float sum = 0;
		for(int i=0; i<16; i++)
			sum += fabsf(identity[i] - (geom->matrix)[i]);
		if(sum > 0.00001 && geom->has_been_drawn == 0)
		{
			printf("\n\n");
			printf("ERROR: You must include a 'uniform mat4 GeomTransform' variable in your GLSL shader (program %d) when you load/display a model with kuhl-util. This matrix should be applied to the vertices in your model before you multiply by your modelview matrix in the vertex program. For example:\n\ngl_Position = Projection * ModelView * GeomTransform * in_Position\n\n", geom->program);
			printf("This matrix is required to correctly translate/rotate/scale your geometry and is also used by some models to implement animation. This matrix is stored inside of a variable called 'matrix' in kuhl_geometry and is set to the identity matrix by default. This message only gets printed if you are using something that actually sets the matrix to something other than the identity. Earlier versions of this software simply transformed the vertices as the file was being loaded instead of doing it in the vertex program.\n");
			printf("\n");
			printf("We would set the GeomTransform to:\n");
			mat4f_print(geom->matrix);
			printf("This program will resume running in 2 seconds...\n");
			sleep(2);
			printf("...continuing despite the missing variable.\n");
		}
	}

	/* Use the vertex array object for this geometry */
	glBindVertexArray(geom->vao);
	kuhl_errorcheck();

	/* kuhl_geometry_attrib_get() allows vertex attribute buffers to
	 * be mapped. Here, we check if the buffers are mapped. If they
	 * are, we unmap them before we draw the geometry. */
	for(unsigned int i=0; i<geom->attrib_count; i++)
	{
		glBindBuffer(GL_ARRAY_BUFFER, geom->attribs[i].bufferobject);
		GLint bufferIsMapped = 0;
		glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_MAPPED, &bufferIsMapped);
		kuhl_errorcheck();
		if(bufferIsMapped)
			glUnmapBuffer(GL_ARRAY_BUFFER);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		kuhl_errorcheck();
	}

	
	/* If the user provided us with indices, use glDrawElements() to
	 * draw the geometry. */
	if(geom->indices_len > 0 && glIsBuffer(geom->indices_bufferobject))
	{
		glDrawElements(geom->primitive_type,
		               geom->indices_len,
		               GL_UNSIGNED_INT,
		               NULL);
		kuhl_errorcheck();
	}
	else
	{
		/* If the user didn't provide us with indices, just draw the
		 * vertices in order. */
		glDrawArrays(geom->primitive_type, 0, geom->vertex_count);
		kuhl_errorcheck();
	}


	/* For each texture unit that we bound a texture to, unbind the
	 * texture since we have finished drawing the geometry */
	for(unsigned int i=0; i<geom->texture_count; i++)
	{
		kuhl_errorcheck();
		/* Turn on appropriate texture unit */
		glActiveTexture(GL_TEXTURE0+i);
		kuhl_errorcheck();
		/* Unbind the texture */
		glBindTexture(GL_TEXTURE_2D, 0);
		kuhl_errorcheck();
	}

	
	/* Indicate in the struct that we have successfully drawn this
	 * geom once. */
	geom->has_been_drawn = 1;

	/* Restore previously active texture */
	glActiveTexture(previouslyActiveTexture);
	
	/* Unbind texture */
	glBindTexture(GL_TEXTURE_2D, previouslyBoundTexture);

	/* Restore the GLSL program that was used before this function was
	 * called. */
	glUseProgram(previouslyUsedProgram);
	
	/* Unbind the VAO */
	glBindVertexArray(previousVAO);
	kuhl_errorcheck();

	/* Draw the next nodes in the list. */
	kuhl_geometry_draw(geom->next);
}

/** Deletes kuhl_geometry struct by freeing the OpenGL buffers that
 * may have been created by kuhl_geometry_attrib() and
 * kuhl_geometry_indices(). It also frees the vertex array object in
 * kuhl_geometry.
 *
 * Important note: kuhl_geometry_init() does not allocate space for
 * textures---so kuhl_geometry_delete() does not delete textures! This
 * behavior is useful in the event that a single texture is shared
 * among several kuhl_geometry structs.
 *
 * @param geom The geometry to free.
*/
void kuhl_geometry_delete(kuhl_geometry *geom)
{
	while(geom->next != NULL)
		kuhl_geometry_delete(geom->next);
	
	for(unsigned int i=0; i<geom->attrib_count; i++)
	{
		kuhl_attrib *attrib = &(geom->attribs[i]);
		if(attrib->name)
			free(attrib->name);
		attrib->name = NULL;
		if(glIsBuffer(attrib->bufferobject))
			glDeleteBuffers(1, &(attrib->bufferobject));
		attrib->bufferobject = 0;
	}
	geom->attrib_count = 0;

	if(glIsBuffer(geom->indices_bufferobject))
		glDeleteBuffers(1, &(geom->indices_bufferobject));
	geom->indices_bufferobject = 0;
	geom->indices_len = 0;
	
	if(glIsVertexArray(geom->vao))
		glDeleteVertexArrays(1, &(geom->vao));
	geom->vao = 0;
	geom->has_been_drawn = 0;
}


/** Converts an array containing RGBA image data into an OpenGL texture using the specified wrapping parameters.
 *
 * @param array Contains a row-major list of pixels in R, G, B, A
 * format starting from the bottom left corner of the image. Each
 * pixel is a value form 0 to 255.
 *
 * @param width The width of the image represented by the array in pixels.
 *
 * @param height The height of the image represented by the array in pixels.
 *
 * @param wrapS The wrapping texture parameter to apply to GL_TEXTURE_WRAP_S.
 *
 * @param wrapT The wrapping texture parameter to apply to GL_TEXTURE_WRAP_T.
 *
 * @return The texture name that you can use with glBindTexture() to
 * enable this particular texture when drawing. When you are done with
 * the texture, use glDeleteTextures(1, &textureName) where
 * textureName is set to the value returned by this function. Returns
 * 0 on error.
 */
GLuint kuhl_read_texture_rgba_array_wrap(const unsigned char* array, int width, int height, GLuint wrapS, GLuint wrapT)
{
	GLuint texName = 0;
	if(!GLEW_VERSION_2_0)
	{
		/* OpenGL 2.0+ supports non-power-of-2 textures. Also, need to
		 * ensure we have a new enough version for the different
		 * mipmap generation techniques below. */
		printf("ERROR: kuhl_read_texture_rgba_array() requires OpenGL 2.0 to generate mipmaps.\n");
		printf("Either your video card/driver doesn't support OpenGL 2.0 or better OR you forgot to call glewInit() at the appropriate time at the beginning of your program.\n");
		return 0;
	}
	kuhl_errorcheck();
	glGenTextures(1, &texName);
	glBindTexture(GL_TEXTURE_2D, texName);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapS);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	kuhl_errorcheck();
	
	/* If anisotropic filtering is available, turn it on.  This does not
	 * override the MIN_FILTER. The MIN_FILTER setting may affect how the
	 * videocard decides to do anisotropic filtering, however.  For more info:
	 * http://www.opengl.org/registry/specs/EXT/texture_filter_anisotropic.txt
	 *
	 * Note that anisotropic filtering may not be available if you ask
	 * for an OpenGL core profile. For more information, see:
	 * http://gamedev.stackexchange.com/questions/70829
	 */
	if(glewIsSupported("GL_EXT_texture_filter_anisotropic"))
	{
		float maxAniso;
		glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, maxAniso);
		msg(DEBUG, "Anisotropic filtering: Available, set to maximum value (%0.1f)\n",
		       maxAniso);
	}

	kuhl_errorcheck();
	
	/* Try to see if OpenGL will accept this texture.  If the dimensions of
	 * the file are too big, OpenGL might not load it. NOTE: The parameters
	 * here should match the parameters of the actual (non-proxy) calls to
	 * glTexImage2D() below. */
	glTexImage2D(GL_PROXY_TEXTURE_2D, 0, GL_RGBA8, width, height,
	             0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	int tmp;
	glGetTexLevelParameteriv(GL_PROXY_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &tmp);
	if(tmp == 0)
	{
		msg(ERROR, "Unable to load %dx%d texture (possibily because it is too large)\n", width, height);
		GLint maxTextureSize = 0;
		glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
		msg(ERROR, "Your card's rough estimate for the maximum texture size that it supports: %dx%d\n", maxTextureSize, maxTextureSize);
		msg(WARNING, "Common max texture sizes for graphics cards can be found at: http://feedback.wildfiregames.com/report/opengl/feature/GL_MAX_TEXTURE_SIZE");

		// According to the website above, as of July 2014, 85% of
		// computers support 8k or larger. Most computers on MTU's
		// campus supports 16k.

		glBindTexture(GL_TEXTURE_2D, 0);
		return 0;
	}

	/* The recommended way to produce mipmaps depends on your OpenGL
	 * version. */
	if (glGenerateMipmap != NULL)
	{
		/* In OpenGL 3.0 or newer, it is recommended that you use
		 * glGenerateMipmaps().  Older versions of OpenGL that provided the
		 * same capability as an extension, called it
		 * glGenerateMipmapsEXT(). */
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height,
		             0, GL_RGBA, GL_UNSIGNED_BYTE, array);
		glGenerateMipmap(GL_TEXTURE_2D);
	}
	else // if(glewIsSupported("GL_SGIS_generate_mipmap"))
	{
		/* Should be used for 1.4 <= OpenGL version < 3.   */
		glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height,
		             0, GL_RGBA, GL_UNSIGNED_BYTE, array);
	}

	/* render textures perspectively correct---instead of interpolating
	   textures in screen-space. */
	kuhl_errorcheck();

	/* The following two lines of code are only useful for OpenGL 1 or
	 * 2 programs. They may cause an error message when called in a
	 * newer version of OpenGL. */
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL); // only use texture, no other lighting applied!
	glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
	glGetError(); // discard any error messages.

	// Unbind the texture, make the caller bind it when they want to use it. More details:
	// http://stackoverflow.com/questions/15273674
	glBindTexture(GL_TEXTURE_2D, 0);
	return texName;
}

/** Converts an array containing RGBA image data into an OpenGL texture using clamping.
 *
 * @param array Contains a row-major list of pixels in R, G, B, A
 * format starting from the bottom left corner of the image. Each
 * pixel is a value form 0 to 255.
 *
 * @param width The width of the image represented by the array in pixels.
 *
 * @param height The height of the image represented by the array in pixels.
 *
 * @return The texture name that you can use with glBindTexture() to
 * enable this particular texture when drawing. When you are done with
 * the texture, use glDeleteTextures(1, &textureName) where
 * textureName is set to the value returned by this function. Returns
 * 0 on error.
 */
GLuint kuhl_read_texture_rgba_array(const unsigned char* array, int width, int height) {
	return kuhl_read_texture_rgba_array_wrap(array, width, height, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE);
}

/** Creates a texture from a string of text. For example, if you want
 * a texture that says "hello world" in red on a transparent
 * background, this method can easily create that texture directly
 * using ImageMagick. The text will be written in a normal font and
 * will be one line of text. The preprocessor variable
 * KUHL_UTIL_USE_IMAGEMAGICK must be defined to use this function.
 *
 * @param label The text that you want to render.
 *
 * @param texName A pointer that will be filled with the OpenGL
 * texture name for the new texture. When you are done with this
 * label, you are responsible for telling OpenGL to delete it with
 * glDeleteTextures(1, &texName).
 *
 * @param color The color of the text.
 *
 * @param bgcolor The background color for the texture (can be transparent).
 *
 * @param pointsize The size of the text in points. Use a larger value for higher resolution text.
 *
 * @return The aspect ratio of the texture. texName is set to 0 and an aspect ratio of 1 is returned. */
float kuhl_make_label(const char *label, GLuint *texName, float color[3], float bgcolor[4], float pointsize)
{
#ifdef KUHL_UTIL_USE_IMAGEMAGICK
	int width = 0;
	int height = 0;
	unsigned char *image = (unsigned char*) image_label(label, &width, &height, color, bgcolor, pointsize);
//	printf("Label texture dimensions: %d %d\n", width, height);
	*texName = kuhl_read_texture_rgba_array(image, width, height);
	free(image);
	
	if(*texName == 0)
		return 1.0f;
	return width/(float)height;
#else
	*texName = 0;
	return 1.0f;
#endif
}


/** This code flips an image vertically. This is helpful since OpenGL
 * puts the first pixel at the bottom left corner and other
 * image-handling libraries may put the pixel in the top left
 * corner. */
void kuhl_flip_texture_rgba_array(unsigned char *image, const int width, const int height, const int components) {
	// printf("Flipping texture with width = %d, height = %d\n", width, height);
	unsigned int bytesPerRow = components * width; // 1 byte per component
	unsigned int pivot = height/2;
	for (unsigned i = 0; i < pivot; ++i) 
	{
		unsigned char *lineTop = (image + i * bytesPerRow);
		unsigned char *lineBottom = (image + (height - i - 1) * bytesPerRow);
		// printf("Swapping %d with %d\n", i, height-i-1);
		for (unsigned j = 0; j < bytesPerRow; ++j)
		{
			unsigned char a = *lineTop;
			unsigned char b = *lineBottom;
			*lineTop = b;
			*lineBottom = a;
			lineTop++;
			lineBottom++;
		}
	}
}


#ifdef KUHL_UTIL_USE_IMAGEMAGICK
static float kuhl_read_texture_file_im(const char *filename, GLuint *texName, GLuint wrapS, GLuint wrapT)
{
	char *newFilename = kuhl_find_file(filename);
	
    /* It is generally best to just load images in RGBA8 format even
     * if we don't need the alpha component. ImageMagick will fill the
     * alpha component in correctly (opaque if there is no alpha
     * component in the file or with the actual alpha data. For more
     * information about why we use RGBA by default, see:
     * http://www.opengl.org/wiki/Common_Mistakes#Image_precision
     */
	imageio_info iioinfo;
	iioinfo.filename   = newFilename;
	iioinfo.type       = CharPixel;
	iioinfo.map        = (char*) "RGBA";
	iioinfo.colorspace = sRGBColorspace;
	unsigned char *image = (unsigned char*) imagein(&iioinfo);
	free(newFilename);
	if(image == NULL)
	{
		msg(ERROR, "Unable to read '%s'.\n", filename);
		return -1;
	}

	/* "image" is a 1D array of characters (unsigned bytes) with four
	 * bytes for each pixel (red, green, blue, alpha). The data in "image"
	 * is in row major order. The first 4 bytes are the color information
	 * for the lowest left pixel in the texture. */
	int width  = (int)iioinfo.width;
	int height = (int)iioinfo.height;
	float aspectRatio = (float)width/height;
	msg(DEBUG, "Finished reading '%s' (%dx%d)\n", filename, width, height);
	*texName = kuhl_read_texture_rgba_array_wrap(image, width, height, wrapS, wrapT);

	if(iioinfo.comment)
		free(iioinfo.comment);
	free(image);
	
	if(*texName == 0)
	{
		msg(ERROR, "Failed to create OpenGL texture from %s\n", filename);
		return -1;
	}

	return aspectRatio;
}
#else // KUHL_UTIL_USE_IMAGEMAGICK

static float kuhl_read_texture_file_stb(const char *filename, GLuint *texName, GLuint wrapS, GLuint wrapT)
{
	char *newFilename = kuhl_find_file(filename);
	
    /* It is generally best to just load images in RGBA8 format even
     * if we don't need the alpha component. STB will fill the
     * alpha component in correctly (opaque if there is no alpha
     * component in the file or with the actual alpha data. For more
     * information about why we use RGBA by default, see:
     * http://www.opengl.org/wiki/Common_Mistakes#Image_precision
     */
	int width  = -1;
	int height = -1;
	int comp = -1;
	int requestedComponents = STBI_rgb_alpha;

	/** STB defaults with the first pixel at the upper left
	 * corner. OpenGL and other packages put the first pixel at the
	 * bottom left corner. But, it allows us to indicate that the
	 * image should be flipped. */
	stbi_set_flip_vertically_on_load(1);
	unsigned char *image = (unsigned char*) stbi_load(newFilename, &width, &height, &comp, requestedComponents);
	free(newFilename);
	if(image == NULL)
	{
		msg(ERROR, "Unable to read '%s'.\n", filename);
		return -1;
	}

	/* "image" is a 1D array of characters (unsigned bytes) with four
	 * bytes for each pixel (red, green, blue, alpha). The data in "image"
	 * is in row major order. The first 4 bytes are the color information
	 * for the lowest left pixel in the texture. */
	float aspectRatio = (float)width/height;
	msg(DEBUG, "Finished reading '%s' (%dx%d)\n", filename, width, height);
	*texName = kuhl_read_texture_rgba_array_wrap(image, width, height, wrapS, wrapT);
	stbi_image_free(image);
	
	if(*texName == 0)
	{
		msg(ERROR, "Failed to create OpenGL texture from %s\n", filename);
		return -1;
	}

	return aspectRatio;
}
#endif // end else part of ifdef KUHL_UTIL_USE_IMAGEMAGICK



/** Uses imageio to read in an image, and binds it to an OpenGL
 * texture name.  Requires OpenGL 2.0 or better.
 *
 * @param filename name of file to load
 *
 * @param texName A pointer to where the OpenGL texture name should be stored.
 * (Remember that the "texture name" is really just some unsigned int).
 *
 * @param wrapS The wrapping texture parameter to apply to GL_TEXTURE_WRAP_S.
 *
 * @param wrapT The wrapping texture parameter to apply to GL_TEXTURE_WRAP_T.
 *
 * @returns The aspect ratio of the image in the file. Since texture
 * coordinates range from 0 to 1, the caller doesn't really need to
 * know how large the image actually is. Returns a negative number on
 * error.
 */
float kuhl_read_texture_file_wrap(const char *filename, GLuint *texName, GLuint wrapS, GLuint wrapT)
{
#ifdef KUHL_UTIL_USE_IMAGEMAGICK
	return kuhl_read_texture_file_im(filename, texName, wrapS, wrapT);
#else
	return kuhl_read_texture_file_stb(filename, texName, wrapS, wrapT);
#endif
}

/** Uses imageio to read in an image, and binds it to an OpenGL
 * texture name.  Requires OpenGL 2.0 or better.
 *
 * @param filename name of file to load
 *
 * @param texName A pointer to where the OpenGL texture name should be stored.
 * (Remember that the "texture name" is really just some unsigned int).
 *
 * @returns The aspect ratio of the image in the file. Since texture
 * coordinates range from 0 to 1, the caller doesn't really need to
 * know how large the image actually is. Returns a negative number on
 * error.
 */
float kuhl_read_texture_file(const char *filename, GLuint *texName)
{
	return kuhl_read_texture_file_wrap(filename, texName, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE);
}

#ifdef KUHL_UTIL_USE_IMAGEMAGICK
static void kuhl_screenshot_im(const char *outputImageFilename)
{
	// Get window size
	int windowWidth  = glutGet(GLUT_WINDOW_WIDTH);
	int windowHeight = glutGet(GLUT_WINDOW_HEIGHT);

	// Allocate space for data from window
	char *data = kuhl_malloc(windowWidth * windowHeight * 3);
	// Read pixels from the window
	glReadPixels(0,0,windowWidth,windowHeight,
	             GL_RGB,GL_UNSIGNED_BYTE, data);
	kuhl_errorcheck();
	// Set up image output settings
	imageio_info info_out;
	info_out.width    = windowWidth;
	info_out.height   = windowHeight;
	info_out.depth    = 8; // bits/color in output image
	info_out.quality  = 85;
	info_out.colorspace = sRGBColorspace;
	info_out.filename = strdup(outputImageFilename);
	info_out.comment  = NULL;
	info_out.type     = CharPixel;
	info_out.map      = "RGB";
	// Write image to disk
	imageout(&info_out, data);
	free(info_out.filename); // cleanup
	free(data);
}
#else // KUHL_UTIL_USE_IMAGEMAGICK

static void kuhl_screenshot_stb(const char *outputImageFilename)
{
	// Get window size
	int windowWidth  = glutGet(GLUT_WINDOW_WIDTH);
	int windowHeight = glutGet(GLUT_WINDOW_HEIGHT);
	int comp = 3; // 3 = RGB, 4 = RGBA
	int stride_in_bytes = windowWidth*comp*sizeof(char);
	// Allocate space for data from window
	unsigned char *data = kuhl_malloc(stride_in_bytes*windowHeight);
	// Read pixels from the window
	glReadPixels(0,0,windowWidth,windowHeight,
	             GL_RGB,GL_UNSIGNED_BYTE, data);
	kuhl_errorcheck();

	kuhl_flip_texture_rgba_array(data, windowWidth, windowHeight, 3);

	int ok=0;
	const char *s = outputImageFilename;
	if(strlen(s) > 4 && !strcmp(s + strlen(s) - 4, ".png"))
		ok = stbi_write_png(s, windowWidth, windowHeight, comp, data, stride_in_bytes);
	else if(strlen(s) > 4 && !strcmp(s + strlen(s) - 4, ".tga"))
		ok = stbi_write_tga(s, windowWidth, windowHeight, comp, data);
	else if(strlen(s) > 4 && !strcmp(s + strlen(s) - 4, ".bmp"))
		ok = stbi_write_bmp(s, windowWidth, windowHeight, comp, data);
	free(data);
	
	if (!ok)
	{
		msg(FATAL, "Failed write screenshot to %s (note: STB can only write png, tga, and bmp files.)\n", outputImageFilename);
		exit(EXIT_FAILURE);
	}

}
#endif // end else part of ifdef KUHL_UTIL_USE_IMAGEMAGICK

/** Takes a screenshot of the current OpenGL screen and writes it to an image file.

    @param outputImageFilename The name of the image file that you want to record the screenshot in. The type of image file is determined by the filename extension. This function will allow you to write to any image format that ImageMagick supports. Suggestion: PNG files often work best for screenshots; try "output.png".
*/
void kuhl_screenshot(const char *outputImageFilename)
{
#ifdef KUHL_UTIL_USE_IMAGEMAGICK
	kuhl_screenshot_im(outputImageFilename);
#else
	kuhl_screenshot_stb(outputImageFilename);
#endif
}

static int kuhl_video_record_frame = 0; // frame that we have recorded.
static time_t kuhl_video_record_prev_sec = 0; // time of previous frame (seconds)
static suseconds_t kuhl_video_record_prev_usec = 0; // time of previous frame usecs

/** Records individual frames to image files that can later be
  combined into a single video file. Call this function every frame
  and it will capture the image data from the frame buffer and write
  it to an image file if enough time has elapsed to record a
  frame. Each image filename will include a frame number. This
  function writes TIFF files to avoid unnecessary computation
  compressing images. Instructions for converting the image files into
  a video file using ffmpeg or avconv will be printed to standard
  out. This may run slowly if you are saving files to a non-local
  filesystem.

    @param fileLabel If fileLabel is set to "label", this function
    will create files such as "label-00000000.tif"
    
    @param fps The number of frames per second to record. Suggested value: 30.
 */
void kuhl_video_record(const char *fileLabel, int fps)
{
#ifdef KUHL_UTIL_USE_IMAGEMAGICK
	char *exten = "tif";
#else
	char *exten = "bmp";
#endif

// Get current time
	struct timeval tv;
	gettimeofday(&tv, NULL);

	if(kuhl_video_record_prev_sec == 0) // first time
	{
		kuhl_video_record_prev_sec  = tv.tv_sec;
		kuhl_video_record_prev_usec = tv.tv_usec;
		printf("%s: Recording %d frames per second\n", __func__, fps);
		printf("Use either of the following commands to assemble Ogg video (Ogg video files are widely supported and not encumbered by patent restrictions):\n");
		printf("ffmpeg -r %d -f image2 -i %s-%%08d.%s -qscale:v 7 %s.ogv\n", fps, fileLabel, exten, fileLabel);
		printf(" - or -\n");
		printf("avconv -r %d -f image2 -i %s-%%08d.%s -qscale:v 7 %s.ogv\n", fps, fileLabel, exten, fileLabel);
		printf("In either program, the -qscale:v parameter sets the quality: 0 (lowest) to 10 (highest)\n");
	}

	time_t sec       = tv.tv_sec;
	suseconds_t usec = tv.tv_usec;

	// useconds between recording frames
	int usecs_over_seconds = 1000000;
	int usec_to_wait = usecs_over_seconds / fps;

	if(kuhl_video_record_prev_sec == sec &&
	   usec - kuhl_video_record_prev_usec < usec_to_wait)
		return; // don't take screenshot
	else if(kuhl_video_record_prev_sec == sec-1 &&
	        (usecs_over_seconds-kuhl_video_record_prev_usec)+usec<usec_to_wait)
		return; // don't take screenshot
	else
	{
		kuhl_video_record_prev_sec  = sec;
		kuhl_video_record_prev_usec = usec;
		char filename[1024];
		snprintf(filename, 1024, "%s-%08d.%s", fileLabel, kuhl_video_record_frame, exten);
		kuhl_screenshot(filename); // TODO: Should we check if the screenshot writes?
		kuhl_video_record_frame++;
	}

}

#ifdef KUHL_UTIL_USE_ASSIMP

/** This struct is used internally by kuhl_util.c to keep track of all textures that are associated with models that have been loaded. */
typedef struct {
	char *textureFileName; /**< The filename of a texture */
	GLuint textureID;      /**< The OpenGL texture name for that texture */
} textureIdMapStruct;
#define textureIdMapMaxSize 1024*32 /**< Maximum number of textures that can be loaded from models */
static textureIdMapStruct textureIdMap[textureIdMapMaxSize]; /**<List of textures for the models */
static int textureIdMapSize = 0; /**< Number of items in textureIdMap */


/** Recursively traverse a tree of ASSIMP nodes and updates the
 * bounding box information.
 *
 * @param nd A pointer to an ASSIMP aiNode struct.
 *
 * @param transform A pointer to an ASSIMP 4x4 transform matrix. When this function is called for the first time, set transform to NULL.
 *
 * @param scene An ASSIMP scene struct.
 *
 * @param bbox The calculated bounding box information (xmin, xmax, ymin, ymax, etc).
 */
static void kuhl_private_calc_bbox(const struct aiNode* nd, struct aiMatrix4x4* transform, const struct aiScene *scene, float bbox[6])
{
	/* When this method is called on the root node, the trafo matrix should be set to NULL. */
	if(transform == NULL)
	{
		bbox[0]=FLT_MAX;
		bbox[1]=-FLT_MAX;
		bbox[2]=FLT_MAX;
		bbox[3]=-FLT_MAX;
		bbox[4]=FLT_MAX;
		bbox[5]=-FLT_MAX;
		
		// Set transform matrix to identity
		struct aiMatrix4x4 ident;
		aiIdentityMatrix4(&ident); 
		transform = &ident;
	}

	// Save the transformation before we process this node.
	struct aiMatrix4x4 previousTransform = *transform;
	// Apply this nodes transformation matrix
	aiMultiplyMatrix4(transform, &nd->mTransformation);

	/* For each mesh */
	for (unsigned int n=0; n < nd->mNumMeshes; ++n)
	{
		const struct aiMesh* mesh = scene->mMeshes[nd->mMeshes[n]];
		/* For each vertex in mesh */
		for (unsigned int t=0; t < mesh->mNumVertices; t++)
		{
			// Transform the vertex based on the transformation matrix
			struct aiVector3D tmp = mesh->mVertices[t];
			aiTransformVecByMatrix4(&tmp, transform);

			// Update our bounding box
			float coord[3];
			vec3f_set(coord, tmp.x, tmp.y, tmp.z);
			if(tmp.x < bbox[0])
				bbox[0] = tmp.x;
			if(tmp.x > bbox[1])
				bbox[1] = tmp.x;
			if(tmp.y < bbox[2])
				bbox[2] = tmp.y;
			if(tmp.y > bbox[3])
				bbox[3] = tmp.y;
			if(tmp.z < bbox[4])
				bbox[4] = tmp.z;
			if(tmp.z > bbox[5])
				bbox[5] = tmp.z;
		}
	}
	
	/* Process the children nodes using the current transformation. */
	for (unsigned int n=0; n < nd->mNumChildren; n++)
		kuhl_private_calc_bbox(nd->mChildren[n], transform, scene, bbox);

	/* Since we are done processing this node, we need to restore the
	* transformation matrix to whatever it was before we started
	* working on this node. */
	*transform = previousTransform;
}


/* Searches a tree of aiNode* structs for a node that matches a given
 * name.
 *
 * @param nodeName Name of the node to look for.
 * @param node Search this node and all children for a node that has the specified name.
 * @return If a matching node is found, this function returns the matching node. Otherwise, returns NULL.
 */
const struct aiNode* kuhl_assimp_find_node(const char *nodeName, const struct aiNode *node)
{
	if(strcmp(node->mName.data, nodeName) == 0)
		return node;
	for(unsigned int i=0; i<node->mNumChildren; i++)
	{
		struct aiNode *child = node->mChildren[i];
		const struct aiNode *foundNode = kuhl_assimp_find_node(nodeName, child);
		if(foundNode != NULL)
			return foundNode;
	}
	return NULL;
}

const struct aiBone* kuhl_assimp_find_bone(const char *nodeName, const struct aiMesh *mesh)
{
	for(unsigned int i=0; i<mesh->mNumBones; i++)
	{
		if(strcmp(mesh->mBones[i]->mName.data, nodeName) == 0)
			return mesh->mBones[i];
	}
	return NULL;
}



/** Used by kuhl_print_aiScene_info() to print out information about
 * all of the nodes in the scene.
 *
 * @param modelFilename The filename of the model (only used to print filename out)
 * @param node An ASSIMP scene object to print information out about.
 * @return Number of nodes underneath and including node.
 */
static int kuhl_print_aiNode_info(const char *modelFilename, const struct aiNode *node)
{
	/* Repeatedly follow parent pointer up to root node, save a string
	 * to indicate the path to this node from the root that we can
	 * print out. */
	char name[2048];
	strncpy(name, node->mName.data, 2048);
	name[2048-1]='\0'; // make sure name is null terminated
	struct aiNode *parent = node->mParent;
	while(parent != NULL)
	{
		char tmp[2048];
		snprintf(tmp, 2048, "%s->%s", parent->mName.data, name);
		strcpy(name, tmp);
		parent = parent->mParent;
	}

	printf("%s: Node \"%s\": meshes=%u children=%u\n", modelFilename, name, node->mNumMeshes, node->mNumChildren);

	int returnVal = 1; // count this node
	
	for(unsigned int i=0; i<node->mNumChildren; i++)
		returnVal += kuhl_print_aiNode_info(modelFilename, node->mChildren[i]);
	
	return returnVal;
}

/** Prints out useful debugging information about an aiScene
 * object. It goes through the array of aiMesh objects and the array
 * of aiAnimation objects stored in the aiScene. It also calls
 * kuhl_print_aiNode_info() to recursively traverse all of the nodes
 * in the scene.
 *
 * @param modelFilename The filename of the model (only used to print filename out)
 * @param scene An ASSIMP scene object to print information out about.
 */
static void kuhl_print_aiScene_info(const char *modelFilename, const struct aiScene *scene)
{
	/* Iterate through the animation information associated with this model */
	for(unsigned int i=0; i<scene->mNumAnimations; i++)
	{
		struct aiAnimation* anim = scene->mAnimations[i];
		printf("%s: Animation #%u: ===================================\n", modelFilename, i);
		printf("%s: Animation #%u: name (probably blank): %s\n", modelFilename, i, anim->mName.data);
		printf("%s: Animation #%u: duration in ticks: %f\n",     modelFilename, i, anim->mDuration);
		printf("%s: Animation #%u: ticks per second: %f\n",      modelFilename, i, anim->mTicksPerSecond);
		printf("%s: Animation #%u: duration in seconds: %f\n",   modelFilename, i, anim->mDuration/anim->mTicksPerSecond);
				
		printf("%s: Animation #%u: number of bone channels: %d\n", modelFilename, i, anim->mNumChannels);
		printf("%s: Animation #%u: number of mesh channels: %d\n", modelFilename, i, anim->mNumMeshChannels);

		// Bones
		for(unsigned int j=0; j<anim->mNumChannels; j++)
		{
			struct aiNodeAnim* animNode = anim->mChannels[j];
			printf("%s: Animation #%u: Bone channel #%u: AffectedNodeName=%s posKeys=%d, rotKeys=%d, scaleKeys=%d\n", modelFilename, i, j,
			       animNode->mNodeName.data,
			       animNode->mNumPositionKeys,
			       animNode->mNumRotationKeys,
			       animNode->mNumScalingKeys);

#if 0
			for(unsigned int i=0; i<animNode->mNumPositionKeys; i++)
			{
				printf("Position %d: %f %f %f time=%f\n", i,
				       animNode->mPositionKeys[i].mValue.x,
				       animNode->mPositionKeys[i].mValue.y,
				       animNode->mPositionKeys[i].mValue.z,
				       animNode->mPositionKeys[i].mTime);
			}

			for(unsigned int i=0; i<animNode->mNumScalingKeys; i++)
			{
				printf("Scale    %d: %f %f %f time=%f\n", i,
				       animNode->mScalingKeys[i].mValue.x,
				       animNode->mScalingKeys[i].mValue.y,
				       animNode->mScalingKeys[i].mValue.z,
				       animNode->mScalingKeys[i].mTime);
			}

			for(unsigned int i=0; i<animNode->mNumRotationKeys; i++)
			{
				printf("Quat     %d: %f %f %f %f (wxyz) time=%f\n", i,
				       animNode->mRotationKeys[i].mValue.w,
				       animNode->mRotationKeys[i].mValue.x,
				       animNode->mRotationKeys[i].mValue.y,
				       animNode->mRotationKeys[i].mValue.z,
				       animNode->mRotationKeys[i].mTime);
			}
#endif
		}

		// Mesh
		for(unsigned int j=0; j<anim->mNumMeshChannels; j++)
		{
			struct aiMeshAnim* animMesh = anim->mMeshChannels[j];
			printf("%s: Animation #%u: Mesh channel #%u: Name of mesh affected: %s\n", modelFilename, i, j, animMesh->mName.data);
			printf("%s: Animation #%u: Mesh channel #%u: Num of keys: %d\n", modelFilename, i, j, animMesh->mNumKeys);
			for(unsigned int k=0; k<animMesh->mNumKeys; k++)
			{
				struct aiMeshKey mkey = animMesh->mKeys[k];
				printf("%s: Animation #%ud: Mesh channel #%u: Key #%u: Time of this mesh key: %f\n", modelFilename, i, j, k, mkey.mTime);
				printf("%s: Animation #%ud: Mesh channel #%u: Key #%u: Index into the mAnimMeshes array: %d\n", modelFilename, i, j, k, mkey.mValue);
			}
		}
	}

	for(unsigned int i=0; i<scene->mNumMeshes; i++)
	{
		struct aiMesh *mesh = scene->mMeshes[i];
		printf("%s: Mesh #%03u: vertices=%u faces=%u bones=%u normals=%s tangents=%s bitangents=%s texcoords=%s name=\"%s\"\n",
		       modelFilename, i, 
		       mesh->mNumVertices,
		       mesh->mNumFaces,
		       mesh->mNumBones,
		       mesh->mNormals          != NULL ? "yes" : "no ",
		       mesh->mTangents         != NULL ? "yes" : "no ",
		       mesh->mBitangents       != NULL ? "yes" : "no ",
		       mesh->mTextureCoords[0] != NULL ? "yes" : "no ", // mTextureCoords is an array of pointers
		       mesh->mName.data
			);

		for(unsigned int j=0; j<mesh->mNumBones; j++)
		{
			struct aiBone *bone = mesh->mBones[j];
			printf("%s: Mesh #%u: Bone #%u: Named \"%s\" and affects %u vertices.\n",
			       modelFilename, i, j, bone->mName.data, bone->mNumWeights);
		}
	}

	int numNodes = kuhl_print_aiNode_info(modelFilename, scene->mRootNode);
	printf("%s: Contains %d node(s) & %u mesh(es)\n", modelFilename, numNodes, scene->mNumMeshes);
}


/** Assimp doesn't store the full path to the textures. Here, we
  assume that the texture path stored in the model is relative to the
  directory the model is stored in. Or, if textureDir is provided, we
  assume that the texture is relative to the textureDir path.

    @param textureFile is the path to the texture stored in the model file.
    
    @param modelFile is the path to the model file. If textureDir is
    NULL, we assume that the texture files are relative to this
    directory.

    @param textureDir is a path that the textures are supposedly
    stored in. This is always used if it is non-NULL. It is ignored if
    it is NULL.

    @return A full path that specifies where the texture file should
    be. The returned string should be free()'d.
*/
static char* kuhl_private_assimp_fullpath(const char *textureFile, const char *modelFile, const char *textureDir)
{
	if(textureFile == NULL || strlen(textureFile) == 0)
	{
		msg(ERROR, "textureFile was NULL or a zero character string.");
		exit(EXIT_FAILURE);
	}
	
	/* Construct a string with the directory that should contain the texture. */
	char *fullpath = malloc(1024);
	if(textureDir == NULL)
	{
		if(modelFile == NULL)
		{
			msg(ERROR, "modelFile was NULL");
			exit(EXIT_FAILURE);
		}
		char *editable = strdup(modelFile);
		char *dname = dirname(editable);
		snprintf(fullpath, 1024, "%s/%s", dname, textureFile);
		free(editable);
	}
	else
		snprintf(fullpath, 1024, "%s/%s", textureDir, textureFile);
	return fullpath;
}


/** Uses ASSIMP to load model (if needed) and returns ASSIMP aiScene
 * object. This function also reads texture files that the model
 * refers to. This function does not create any kuhl_geometry structs
 * for the model.
 *
 * @param modelFilename The filename of a model to load.
 *
 * @param textureDirname The directory the textures for the model are
 * stored in. If textureDirname is NULL, we assume that the textures
 * are in the same directory as the model file.
 *
 * @return An ASSIMP aiScene object for the requested model. Returns
 * NULL on error.
 */
static const struct aiScene* kuhl_private_assimp_load(const char *modelFilename, const char *textureDirname)
{
	/* If we get here, we need to add the file to the sceneMap. */
	msg(INFO, "Loading model: %s\n", modelFilename);

	/* Write assimp messages to msg log */
	struct aiLogStream stream;
	stream = aiGetPredefinedLogStream(aiDefaultLogStream_STDOUT,NULL);
	if(stream.callback != msg_assimp_callback)
	{
		// we only need to set this up once.
		stream.callback = msg_assimp_callback;
		stream.user = strdup(modelFilename); // memory leak
		aiAttachLogStream(&stream);
	}
	
	/* Try loading the model. We are using a postprocessing preset
	 * here so we don't have to set many options. */
	char *modelFilenameVarying = strdup(modelFilename); // aiImportFile doesn't declare filaname parameter as const!

	/* We will load the file and do significant processing (split
	 * large meshes into smaller ones, triangulate polygons in meshes,
	 * apply transformation matrices. For more information about model
	 * loading options, see:
	 * http://assimp.sourceforge.net/lib_html/postprocess_8h.html
	 *
	 * The postprocess procedures can greatly influence how long it
	 * takes to load a model. If you are trying to load a large model,
	 * try setting the post-process settings to 0.
	 *
	 * Other options which trigger multiple other options:
	 * aiProcessPreset_TargetRealtime_Fast
	 * aiProcessPreset_TargetRealtime_Quality
	 * aiProcessPreset_TargetRealtime_MaxQuality
	 *
	 * Individual options:
	 * 0                     - do nothing
	 * aiProcess_Triangulate - Triangulate polygons into triangles
	 * aiProcess_SortByPType - Put each primitive type into its own mesh
	 * aiProcess_GenNormals  - Generate flat normals if normals aren't present in file
	 * aiProcess_GenSmoothNormals - Generate smooth normals if normals aren't present in file
	 * aiProcess_LimitBoneWeights - Limits bone weights per vertex to 4
	 * aiProcess_JoinIdenticalVertices - Ensures that the model uses an index buffer.
	 * aiProcess_PreTransformVertices - Pretransforms all vertices according to matrices in the model file
	 */

	// If we are generating smooth normals, don't smooth edges that
	// are 80 degrees or higher (i.e., use flat normals on a cube).
	struct aiPropertyStore* propStore = aiCreatePropertyStore();
	aiSetImportPropertyFloat(propStore, "PP_GSN_MAX_SMOOTHING_ANGLE", 50.0f);
	// Import/load the model
	int aiProcessFlags = aiProcess_Triangulate|aiProcess_SortByPType; // required! Use only these flags for fast loading.
	// aiProcessFlags |= aiProcessPreset_TargetRealtime_Fast;    // a bit slower, adds additional processing
	aiProcessFlags |= aiProcessPreset_TargetRealtime_Quality; // Does even more processing during model load.
	const struct aiScene* scene = aiImportFileExWithProperties(modelFilenameVarying, aiProcessFlags, NULL, propStore);
	free(modelFilenameVarying);
	if(scene == NULL)
		return NULL;

	/* Print warning messages if the model uses features that our code
	 * doesn't support (even though ASSIMP might support them. */
	if(scene->mNumCameras > 0)
		msg(DEBUG, "%s: This model has %u camera(s) embedded in it that we are ignoring.\n",
		       modelFilename, scene->mNumCameras);
	if(scene->mNumLights > 0)
		msg(DEBUG, "%s: This model has %u light(s) embedded in it that we are ignoring.\n",
		       modelFilename, scene->mNumLights);
	if(scene->mNumTextures > 0)
		msg(DEBUG, "%s: This model has %u texture(s) embedded in it. This program currently ignores embedded textures.\n",
		       modelFilename, scene->mNumTextures);

	// Uncomment this line to print additional information about the model:
	// kuhl_print_aiScene_info(modelFilename, scene);

	/* For safety, zero out our texture ID map if it is supposed to be empty right now. */
	if(textureIdMapSize == 0)
	{
		for(int i=0; i<textureIdMapMaxSize; i++)
		{
			textureIdMap[i].textureFileName = NULL;
			textureIdMap[i].textureID = 0;
		}
	}

	/* For each material that has a texture in the scene, try to load the corresponding texture file. */
	for(unsigned int m=0; m < scene->mNumMaterials; m++)
	{
		struct aiString path;
		GLuint texIndex = 0;


		if(aiGetMaterialTexture(scene->mMaterials[m], aiTextureType_DIFFUSE,  texIndex, &path, NULL, NULL, NULL, NULL, NULL, NULL) == AI_SUCCESS)
		{
			/* Don't load a texture that we have already loaded. */
			char *fullpath = kuhl_private_assimp_fullpath(path.data, modelFilename, textureDirname);
			int alreadyExists = 0;
			for(int i=0; i<textureIdMapSize; i++)
			{
				if(strcmp(fullpath, textureIdMap[i].textureFileName) == 0)
				{
					alreadyExists=1;
					break;
				}
			}
			if(alreadyExists > 0) // no need to reload an already loaded texture
				continue;

			if(kuhl_read_texture_file(fullpath, &texIndex) < 0)
			{
				printf("%s: WARNING: %s refers to texture %s which we could not find at %s\n", __func__, modelFilename, path.data, fullpath);
			}

			/* Store the texture information in our list structure so
			 * we can find the textureID from the filename when we
			 * render the scene. */
			if(textureIdMapSize >= textureIdMapMaxSize)
			{
				fprintf(stderr, "%s: You have loaded more textures than the hardcoded limit. Exiting.\n", __func__);
				exit(EXIT_FAILURE);
			}
			textureIdMap[textureIdMapSize].textureFileName = strdup(fullpath);
			textureIdMap[textureIdMapSize].textureID = texIndex;
			textureIdMapSize++;
		}

		/* If we failed to load a diffuse texture and there are no
		 * other textures, we shouldn't print a
		 * warning/error. However, if there is no diffuse and
		 * other textures exist, print a message and exit so the
		 * user knows that non-diffuse textures aren't going to be
		 * loaded. */
		#define TEX_TYPE_LEN 12
		static const enum aiTextureType texTypeList[TEX_TYPE_LEN] = {
			aiTextureType_DIFFUSE,   aiTextureType_SPECULAR,     aiTextureType_AMBIENT,
			aiTextureType_EMISSIVE,  aiTextureType_HEIGHT,       aiTextureType_NORMALS,
			aiTextureType_SHININESS, aiTextureType_OPACITY,      aiTextureType_DISPLACEMENT,
			aiTextureType_LIGHTMAP,  aiTextureType_REFLECTION,   aiTextureType_UNKNOWN };
		
		static const char *texTypeListStr[TEX_TYPE_LEN] = {"DIFFUSE", "SPECULAR", "AMBIENT",
		                                                   "EMISSIVE", "HEIGHT", "NORMALS",
		                                                   "SHININESS","OPACITY","DISPLACEMENT",
		                                                   "LIGHTMAP","REFLECTION","UNKNOWN" };

		int textureCount = 0;
		for(int i=1; i<TEX_TYPE_LEN; i++) // skip diffuse
			textureCount += aiGetMaterialTextureCount(scene->mMaterials[m], texTypeList[i]);
		if(textureCount > 0) // if there is a non-diffuse texture
		{
			char buf[1024] = "";
			int buflen = 0;
			buflen += snprintf(buf+buflen, 1024-buflen, "Ignoring some textures in material: ");
			for(int i=1; i<TEX_TYPE_LEN; i++)
			{
				int count = aiGetMaterialTextureCount(scene->mMaterials[m], texTypeList[i]);
				if(count > 0)
					buflen += snprintf(buf+buflen, 1024-buflen, "%s=%d ", texTypeListStr[i], count);
			}
			msg(DEBUG, "%s", buf);

			if(aiGetMaterialTextureCount(scene->mMaterials[m], texTypeList[0]) > 1)
				msg(DEBUG, "The material also has more than one diffuse texture.\n");
		}
	}

	return scene;
}

/** Given a aiNodeAnim object and a time, return an appropriate
 * transformation matrix.
 *
 * @param transformResult The resulting transformation matrix.
 * @param na The aiNodeAnim to generate the matrix form.
 * @param ticks The time of the animation in TICKS (not seconds!)
 */
static void kuhl_private_anim_matrix(float transformResult[16], const struct aiNodeAnim *na, double ticks)
{

	/* Find indices of start and stop position keys */
	unsigned int positionStart = 0;
	for(unsigned int j=0; j<na->mNumPositionKeys-1; j++)
		if(ticks < na->mPositionKeys[j+1].mTime)
		{
			positionStart = j;
			break;
		}
	unsigned int positionEnd = positionStart+1;
	if(positionEnd >= na->mNumPositionKeys)
		positionEnd = positionStart;
	/* Determine where we are in relation to the two nearest keys */
	float deltaTime = na->mPositionKeys[positionEnd].mTime - na->mPositionKeys[positionStart].mTime;
	float factor;
	if(deltaTime != 0)
		factor = (ticks - na->mPositionKeys[positionStart].mTime)/deltaTime;
	else
		factor = 0;
	
	/* Interpolate between two nearest keys */
	float positionValStart[3] = { na->mPositionKeys[positionStart].mValue.x,
	                              na->mPositionKeys[positionStart].mValue.y,
	                              na->mPositionKeys[positionStart].mValue.z };
	float positionValEnd[3] = { na->mPositionKeys[positionEnd].mValue.x,
	                            na->mPositionKeys[positionEnd].mValue.y,
	                            na->mPositionKeys[positionEnd].mValue.z };
	float positionValMid[3];
	vec3f_scalarMult(positionValStart, (1-factor));
	vec3f_scalarMult(positionValEnd, factor);
	vec3f_add_new(positionValMid, positionValStart, positionValEnd);
	float positionMatrix[16];
	mat4f_translateVec_new(positionMatrix, positionValMid);

	/* Find indices of start and stop rotation keys */
	unsigned int rotationStart = 0;
	for(unsigned int j=0; j<na->mNumRotationKeys-1; j++)
		if(ticks < na->mRotationKeys[j+1].mTime)
		{
			rotationStart = j;
			break;
		}
	unsigned int rotationEnd = rotationStart+1;
	if(rotationEnd >= na->mNumRotationKeys)
		rotationEnd = rotationStart;
	/* Determine where we are in relation to the two nearest keys */
	deltaTime = na->mRotationKeys[rotationEnd].mTime - na->mRotationKeys[rotationStart].mTime;
	if(deltaTime != 0)
		factor = (ticks - na->mRotationKeys[rotationStart].mTime)/deltaTime;
	else
		factor = 0;
	/* Interpolate between two nearest keys */
	float rotationValStart[4] = { na->mRotationKeys[rotationStart].mValue.x,
	                              na->mRotationKeys[rotationStart].mValue.y,
	                              na->mRotationKeys[rotationStart].mValue.z,
	                              na->mRotationKeys[rotationStart].mValue.w };
	float rotationValEnd[4] = { na->mRotationKeys[rotationEnd].mValue.x,
	                            na->mRotationKeys[rotationEnd].mValue.y,
	                            na->mRotationKeys[rotationEnd].mValue.z,
	                            na->mRotationKeys[rotationEnd].mValue.w };
	float rotationValMid[4];
	//vec4f_normalize(rotationValStart);
	//vec4f_normalize(rotationValEnd);
	quatf_slerp_new(rotationValMid, rotationValStart, rotationValEnd, factor);
	float rotationMatrix[16];
	mat4f_rotateQuatVec_new(rotationMatrix, rotationValMid);

	/* Find indices of start and stop scaling keys */
	unsigned int scalingStart = 0;
	for(unsigned int j=0; j<na->mNumScalingKeys-1; j++)
		if(ticks < na->mScalingKeys[j+1].mTime)
		{
			scalingStart = j;
			break;
		}
	unsigned int scalingEnd = scalingStart+1;
	if(scalingEnd >= na->mNumScalingKeys)
		scalingEnd = scalingStart;
	/* Determine where we are in relation to the two nearest keys */
	deltaTime = na->mScalingKeys[scalingEnd].mTime - na->mScalingKeys[scalingStart].mTime;
	if(deltaTime != 0)
		factor = (ticks - na->mScalingKeys[scalingStart].mTime)/deltaTime;
	else
		factor = 0;
	/* Interpolate between two nearest keys */
	float scalingValStart[3] = { na->mScalingKeys[scalingStart].mValue.x,
	                             na->mScalingKeys[scalingStart].mValue.y,
	                             na->mScalingKeys[scalingStart].mValue.z };
	float scalingValEnd[3] = { na->mScalingKeys[scalingEnd].mValue.x,
	                           na->mScalingKeys[scalingEnd].mValue.y,
	                           na->mScalingKeys[scalingEnd].mValue.z };
	float scalingValMid[3];
	vec3f_scalarMult(scalingValStart, (1-factor));
	vec3f_scalarMult(scalingValEnd, factor);
	vec3f_add_new(scalingValMid, scalingValStart, scalingValEnd);
	float scalingMatrix[16];
	mat4f_scaleVec_new(scalingMatrix, scalingValMid);
	
	// transformResult = translation * rotation * scaling
	mat4f_mult_mat4f_new(transformResult, positionMatrix, rotationMatrix);
	mat4f_mult_mat4f_new(transformResult, transformResult, scalingMatrix);
}

/* Returns the transformation matrix for a node (without considering
 * the transformations of the parent node). If there is no animation
 * information, the matrix is stored in the node itself. If there is
 * animation information, we ignore the matrix in the node and instead
 * calculate a matrix based on the animation information.
 *
 * @param transformResult To be filled in with the matrix for the
 * requested node.
 *
 * @param scene The ASSIMP scene object containing the node.
 *
 * @param node The ASSIMP node object that we want animation
 * information about.
 *
 * @param animationNum If the file contains more than one animation,
 * indicates which animation to use. If you don't know, set this to 0.
 *
 * @param t The time in seconds that you want the animation matrix
 * for. If time is negative, this function is guaranteed to return the
 * transformation matrix in the node.
 *
 * @return Returns 1 if we successfully returned a matrix based on
 * animation information. Returns 0 if we simply returned the
 * transformation matrix in the node itself.
 */
static int kuhl_private_node_matrix(float transformResult[16],
                                    const struct aiScene *scene,
                                    const struct aiNode *node,
                                    unsigned int animationNum, double t)
{
	/* Copy the transform matrix from the node itself. This is the
	 * matrix that the user will see if we are unable to find the
	 * requested animation matrix for this node. */
	mat4f_from_aiMatrix4x4(transformResult, node->mTransformation);
	
	/* Return the transformation matrix from the node if: (1) The
	 * requested animation number is too large. (2) A negative time
	 * value is requested. */
	if(animationNum >= scene->mNumAnimations || t < 0)
		return 0;

	struct aiAnimation *anim = scene->mAnimations[animationNum];

	/* If the time value too large for the animation, return the
	 * transformation matrix form the node. */
	double currentTick = t * anim->mTicksPerSecond;
	if(currentTick > anim->mDuration)
		return 0;

	/* Find the channel corresponding to the node name passed in as
	 * parameter. */
	for(unsigned int i=0; i<anim->mNumChannels; i++)
	{
		if(strcmp(anim->mChannels[i]->mNodeName.data, node->mName.data) == 0)
		{
			/* Get this node's matrix according to the animation
			 * information. */
			struct aiNodeAnim *na = anim->mChannels[i];
			kuhl_private_anim_matrix(transformResult, na, currentTick);
			return 1;
		}
	}

	return 0;
}


/* Appends two kuhl_geometry lists together and returns the first item
 * in the list.
 *
 * @param a The list of geometry that should be appended to.
 *
 * @param b The lits of geometry to append to the end of the 'a' list.
 *
 * @return The first kuhl_geometry object in the appended
 * list. Typically, this will equal 'a'. This function returns NULL if
 * both 'a' and 'b' are null. It returns 'b' if only 'a' is null.
 */
kuhl_geometry* kuhl_geometry_append(kuhl_geometry *a, kuhl_geometry *b)
{
	if(a == NULL && b == NULL)
		return NULL;
	if(a == NULL && b != NULL)
		return b;
	if(a != NULL && b == NULL)
		return a;

	kuhl_geometry *origA = a;
	while(a->next != NULL)
		a = a->next;
	a->next = b;
	return origA;
}



/** Recursively calls itself to create one or more kuhl_geometry
 * structs for all of the nodes in the scene.
 *
 * @param sc The scene that we want to render.
 *
 * @param nd The current node that we are rendering.
 */
static kuhl_geometry* kuhl_private_load_model(const struct aiScene *sc,
                                              const struct aiNode* nd,
                                              GLuint program,
                                              float currentTransform[16],
                                              const char* modelFilename,
                                              const char* textureDirname)
{
	/* Each node in the scene has a transform matrix that should
	 * affect all of the nodes under it. The currentTransform matrix
	 * is the current matrix based on any nodes above the one that we
	 * are currently processing. Here, we update the currentTransform
	 * to include the matrix in the node we are currently on. */

	/* Save our current transform so we can reapply it when we are
	 * finished processing this node */
	float origTransform[16];
	mat4f_copy(origTransform, currentTransform);
	
	/* Get this node's transform matrix and convert it into a plain array. */
	float thisTransform[16];
	mat4f_from_aiMatrix4x4(thisTransform, nd->mTransformation);

	/* Apply this node's transformation to our current transform. */
	mat4f_mult_mat4f_new(currentTransform, currentTransform, thisTransform);

	kuhl_geometry *first_geom = NULL;
	
	/* Create a kuhl_geometry object for each of the meshes assigned
	 * to this ASSIMP node. */
	for(unsigned int n=0; n < nd->mNumMeshes; n++)
	{
		const struct aiMesh* mesh = sc->mMeshes[nd->mMeshes[n]];

		/* Confirm that the mesh has only one primitive type. */
		if(mesh->mPrimitiveTypes == 0)
		{
			printf("%s: ERROR: Primitive type not set by ASSIMP in mesh.\n", __func__);
			continue;
		}
		// Check if more than one bit (i.e., primitive type) is in this mesh.
		if((mesh->mPrimitiveTypes & (mesh->mPrimitiveTypes-1)) != 0) 
		{
			printf("%s: ERROR: This mesh has more than one primitive "
			       "type in it. The model should be loaded with the "
			       "aiProcess_SortByPType flag set.\n", __func__);
			continue;
		}

		/* We assume that each mesh has its own primitive type. Here
		 * we identify that type by number and by the OpenGL name.. */
		unsigned int meshPrimitiveType;
		int meshPrimitiveTypeGL;
		if(mesh->mPrimitiveTypes & aiPrimitiveType_POINT)
		{
			meshPrimitiveType = 1;
			meshPrimitiveTypeGL = GL_POINTS;
		}
		else if(mesh->mPrimitiveTypes & aiPrimitiveType_LINE)
		{
			meshPrimitiveType = 2;
			meshPrimitiveTypeGL = GL_LINES;
		}
		else if(mesh->mPrimitiveTypes & aiPrimitiveType_TRIANGLE)
		{
			meshPrimitiveType = 3;
			meshPrimitiveTypeGL = GL_TRIANGLES;
		}
		else if(mesh->mPrimitiveTypes & aiPrimitiveType_POLYGON)
		{
			msg(WARNING, "Mesh %u (%u/%u meshes in node \"%s\"): We only "
			       "support drawing triangle, line, or point meshes. "
			       "This mesh contained polygons, and we are skipping it. "
			       "To resolve this problem, ensure that the file is loaded "
			       "with aiProcess_Triangulage to force ASSIMP to triangulate "
			       "the model.\n",
			       nd->mMeshes[n], n+1, nd->mNumMeshes, nd->mName.data);
			continue;
		}
		else
		{
			msg(ERROR, "Unknown primitive type in mesh.\n");
			continue;
		}
		
		/* Allocate space and initialize kuhl_geometry. One kuhl_geometry
		 * will be used per mesh. We
		 * allocate each one individually (instead of malloc()'ing one
		 * large space for all of the meshes in this node so each of
		 * the objects can be free()'d) */
		kuhl_geometry *geom = (kuhl_geometry*) kuhl_malloc(sizeof(kuhl_geometry));
		kuhl_geometry_new(geom, program, mesh->mNumVertices,
		                  meshPrimitiveTypeGL);

		/* Set up kuhl_geometry linked list */
		first_geom = kuhl_geometry_append(first_geom, geom);

		geom->assimp_node = (struct aiNode*) nd;
		geom->assimp_scene = (struct aiScene*) sc;
		mat4f_copy(geom->matrix, currentTransform);

		/* Store the vertex position attribute into the kuhl_geometry struct */
		float *vertexPositions = kuhl_malloc(sizeof(float)*mesh->mNumVertices*3);
		for(unsigned int i=0; i<mesh->mNumVertices; i++)
		{
			vertexPositions[i*3+0] = (mesh->mVertices)[i].x;
			vertexPositions[i*3+1] = (mesh->mVertices)[i].y;
			vertexPositions[i*3+2] = (mesh->mVertices)[i].z;
		}
		kuhl_geometry_attrib(geom, vertexPositions, 3, "in_Position", 0);
		free(vertexPositions);

		/* Store the normal vectors in the kuhl_geometry struct */
		if(mesh->mNormals != NULL)
		{
			float *normals = kuhl_malloc(sizeof(float)*mesh->mNumVertices*3);
			for(unsigned int i=0; i<mesh->mNumVertices; i++)
			{
				normals[i*3+0] = (mesh->mNormals)[i].x;
				normals[i*3+1] = (mesh->mNormals)[i].y;
				normals[i*3+2] = (mesh->mNormals)[i].z;
			}
			kuhl_geometry_attrib(geom, normals, 3, "in_Normal", 0);
			free(normals);
		}

		/* Store the vertex color attribute */
		// Note: mesh->mColors is a C array, not a pointer
		if(mesh->mColors[0] != NULL)
		{
			static const int colorComps = 3; // don't use alpha by default.
			float *colors = kuhl_malloc(sizeof(float)*mesh->mNumVertices*colorComps);
			for(unsigned int i=0; i<mesh->mNumVertices; i++)
			{
				colors[i*colorComps+0] = mesh->mColors[0][i].r;
				colors[i*colorComps+1] = mesh->mColors[0][i].g;
				colors[i*colorComps+2] = mesh->mColors[0][i].b;
				if(colorComps == 4)
					colors[i*colorComps+3] = mesh->mColors[0][i].a;
			}
			kuhl_geometry_attrib(geom, colors, colorComps, "in_Color", 0);
			free(colors);
		}
		/* If there are no vertex colors, try to use material colors instead */
		else
		{
			/* It would be more efficient to send material colors as a
			 * uniform variable. However, by using this approach, we
			 * don't need to use both a material color uniform and a
			 * vertex color attribute in a GLSL program that displays
			 * a model. */
			const struct aiMaterial *mtl = sc->mMaterials[mesh->mMaterialIndex];
			struct aiColor4D diffuse;
			if(AI_SUCCESS == aiGetMaterialColor(mtl, AI_MATKEY_COLOR_DIFFUSE, &diffuse))
			{
				float *colors = kuhl_malloc(sizeof(float)*mesh->mNumVertices*3);
				for(unsigned int i=0; i<mesh->mNumVertices; i++)
				{
					colors[i*3+0] = diffuse.r;
					colors[i*3+1] = diffuse.g;
					colors[i*3+2] = diffuse.b;
				}
				kuhl_geometry_attrib(geom, colors, 3, "in_Color", 0);
			}
		}
		
		/* Store the texture coordinate attribute */
		// Note: mesh->mTextureCoords is a C array, not a pointer
		if(mesh->mTextureCoords[0] != NULL)
		{
			float *texCoord = kuhl_malloc(sizeof(float)*mesh->mNumVertices*2);
			for(unsigned int i=0; i<mesh->mNumVertices; i++)
			{
				texCoord[i*2+0] = mesh->mTextureCoords[0][i].x;
				texCoord[i*2+1] = mesh->mTextureCoords[0][i].y;
			}
			kuhl_geometry_attrib(geom, texCoord, 2, "in_TexCoord", 1);
			free(texCoord);
		}

		/* Fill in bone information */
		if(mesh->mBones != NULL && mesh->mNumBones > 0)
		{
			if(mesh->mNumBones > MAX_BONES)
			{
				printf("%s: This mesh has %d bones but we only support %d\n",
				       __func__, mesh->mNumBones, MAX_BONES);
				exit(EXIT_FAILURE);
			}
			
			float *indices = kuhl_malloc(sizeof(float)*mesh->mNumVertices*4);
			float *weights = kuhl_malloc(sizeof(float)*mesh->mNumVertices*4);
			/* For each vertex */
			for(unsigned int i=0; i<mesh->mNumVertices; i++)
			{
				/* Zero out weights */
				for(int j=0; j<4; j++)
				{
					// If weight is zero, it doesn't matter what the index
					// is as long as it isn't out of bounds.
					indices[i*4+j] = 0;
					weights[i*4+j] = 0;
				}

				int count = 0; /* How many bones refer to this vertex? */
					
				/* For each bone */
				for(unsigned int j=0; j<mesh->mNumBones; j++)
				{
					/* Each vertex that this bone refers to. */
					for(unsigned int k=0; k<mesh->mBones[j]->mNumWeights; k++)
					{
						/* If this bone refers to a vertex that matches the one
						   that we are on, use the data and send it to the vertex program.
						 */
						unsigned int idx = mesh->mBones[j]->mWeights[k].mVertexId;
						float wght       = mesh->mBones[j]->mWeights[k].mWeight;
						if(idx == i)
						{
							indices[i*4+count] = (float) j;
							weights[i*4+count] = wght;
							count++;
						} // end if vertices match
					} // end for each vertex the bone refers to
				} // end for each bone
			} // end for each vertex in mesh

			for(unsigned int i=0; i<mesh->mNumVertices; i++)
			{
				if(weights[i*4+0] == 0)
				{
					fprintf(stderr, "%s: ERROR Every vertex should have at least one weight but vertex %ud has no weights!\n", __func__, i);
					exit(EXIT_FAILURE);
				}
			}
			kuhl_geometry_attrib(geom, indices, 4, "in_BoneIndex", 0);
			kuhl_geometry_attrib(geom, weights, 4, "in_BoneWeight", 0);
			free(indices);
			free(weights);
		} // end if there are bones 
		
		/* Find our texture and tell our kuhl_geometry object about
		 * it. */
		struct aiString texPath;	//contains filename of texture
		int texIndex = 0;
		if(AI_SUCCESS == aiGetMaterialTexture(sc->mMaterials[mesh->mMaterialIndex],
		                                      aiTextureType_DIFFUSE, texIndex, &texPath,
		                                      NULL, NULL, NULL, NULL, NULL, NULL))
		{
			GLuint texture = 0;
			for(int i=0; i<textureIdMapSize; i++)
			{
				char *fullpath = kuhl_private_assimp_fullpath(texPath.data, modelFilename, textureDirname);
				if(strcmp(textureIdMap[i].textureFileName, fullpath) == 0)
					texture = textureIdMap[i].textureID;
				free(fullpath);
			}
			if(texture == 0)
			{
				printf("%s: WARNING: Mesh %u uses texture '%s'. This texture should have been loaded earlier, but we can't find it now.\n",
				       __func__, nd->mMeshes[n], texPath.data);
			}
			else
			{
				/* If model uses texture and we found the texture file,
				   Make sure we repeat instead of clamp textures */
				glBindTexture(GL_TEXTURE_2D, texture);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
				kuhl_errorcheck();

				kuhl_geometry_texture(geom, texture, "tex", 0);
			}
		}

		if(mesh->mNumFaces > 0)
		{
			/* Get indices to draw with */
			GLuint numIndices = mesh->mNumFaces * meshPrimitiveType;
			GLuint *indices = kuhl_malloc(sizeof(GLuint)*numIndices);
			for(unsigned int t = 0; t<mesh->mNumFaces; t++) // for each face
			{
				const struct aiFace* face = &mesh->mFaces[t];
				for(unsigned int x = 0; x < meshPrimitiveType; x++) // for each index
					indices[t*meshPrimitiveType+x] = face->mIndices[x];
			}
			kuhl_geometry_indices(geom, indices, numIndices);
			free(indices);
		}


		/* Initialize list of bone matrices if this mesh has bones. */
		if(mesh->mNumBones > 0)
		{
			kuhl_bonemat *bones = (kuhl_bonemat*) kuhl_malloc(sizeof(kuhl_bonemat));
			bones->count = mesh->mNumBones;
			bones->mesh = n;
			for(unsigned int b=0; b < mesh->mNumBones; b++)
				bones->boneList[b] = mesh->mBones[b];
			// set any unused bone matrices to the identity.
			for(unsigned int b=mesh->mNumBones; b < MAX_BONES; b++)
				mat4f_identity(bones->matrices[b]);
			geom->bones = bones;
		}

		msg(DEBUG, "Mesh #%03u in node \"%s\" (node has %d meshes): verts=%d indices=%d primType=%d normals=%s colors=%s texCoords=%s bones=%d tex=%s\n",
		       nd->mMeshes[n], nd->mName.data, nd->mNumMeshes,
		       mesh->mNumVertices,
		       mesh->mNumFaces*meshPrimitiveType,
		       meshPrimitiveType,
		       mesh->mNormals       == NULL ? "n" : "y",
		       mesh->mColors[0]==NULL ? "n" : "y", // mColors is an array of pointers
		       mesh->mTextureCoords[0] == NULL ? "n" : "y",   // mTextureCoords is an array of pointers
		       mesh->mNumBones,
		       geom->texture_count == 0 ? "(null)" : texPath.data);
	} // end for each mesh in node

	/* Process all of the meshes in the aiNode's children too */
	for (unsigned int i = 0; i < nd->mNumChildren; i++)
	{
		kuhl_geometry *child_geom = kuhl_private_load_model(sc, nd->mChildren[i], program, currentTransform, modelFilename, textureDirname);
		first_geom = kuhl_geometry_append(first_geom, child_geom);
	}

	/* Restore the transform matrix to exactly as it was when this
	 * function was called by the caller. */
	mat4f_copy(currentTransform, origTransform);

	return first_geom;
}


/** Setup a model to draw at a specific time.

    @param modelFilename Name of model file to update.

    @param animationNum The animation to use. If the file only
    contains one animation, set it to 0.

    @param time The time in seconds to set the animation to. Setting
    time to a negative displays the model in its bind pose.
*/
void kuhl_update_model(kuhl_geometry *first_geom, unsigned int animationNum, float time)
{
	for(kuhl_geometry *g = first_geom; g != NULL; g=g->next)
	{
		/* The aiScene object that this kuhl_geometry refers to. */
		struct aiScene *scene = g->assimp_scene;
		/* The aiNode object that this kuhl_geometry refers to. */
		struct aiNode *node = g->assimp_node;

		/* If the geometry contains no animations, isn't associated
		 * with an ASSIMP scene or node, then there is no need to try
		 * to animate it. */
		if(scene == NULL || scene->mNumAnimations == 0 || node == NULL)
			continue;

		/* If there are no bones, or if a negative time value was
		 * provided, update g->matrix. If there are bones, we assume
		 * that the bones will drive the animation. */
		if(g->bones == NULL )
		{
			/* Start at our current node and traverse up. Apply all of the
			 * transformation matrices as we traverse up.
			 *
			 * TODO: By repeatedly traversing up, we repeatedly
			 * recalculate the transformation matrices for the nodes
			 * near the root---potentially reducing performance.
			 */
			mat4f_identity(g->matrix);
			do
			{
				float transform[16];
				kuhl_private_node_matrix(transform, scene, node, animationNum, time);
				mat4f_mult_mat4f_new(g->matrix, transform, g->matrix);
				node = node->mParent;
			} while(node != NULL);
		}

		/* Don't process bones if there aren't any. */
		if(g->bones == NULL)
			continue;

		/* Update the list of bone matrices. */
		for(int b=0; b < g->bones->count; b++) // For each bone
		{
			// Find the bone node and the bone itself.
			const struct aiNode *node = kuhl_assimp_find_node(g->bones->boneList[b]->mName.data, scene->mRootNode);
			if(node == NULL)
			{
				msg(ERROR, "Failed to find node that corresponded to bone: %s\n", g->bones->boneList[b]->mName.data);
				exit(EXIT_FAILURE);
			}
			const struct aiBone *bone = g->bones->boneList[b];


			/* Start at our current node and traverse up. Apply all of the
			 * transformation matrices as we traverse up.
			 *
			 * TODO: By repeatedly traversing up, we repeatedly
			 * recalculate the transformation matrices for the nodes
			 * near the root---potentially reducing performance.
			 */
			mat4f_identity(g->bones->matrices[b]);
			do
			{
				float transform[16];
				kuhl_private_node_matrix(transform, scene, node, animationNum, time);
				mat4f_mult_mat4f_new(g->bones->matrices[b], transform, g->bones->matrices[b]);
				node = node->mParent; // move to next node up
			} while(node != NULL);

			/* Also apply the bone offset */
			float offset[16];
			mat4f_from_aiMatrix4x4(offset, bone->mOffsetMatrix);
			mat4f_mult_mat4f_new(g->bones->matrices[b], g->bones->matrices[b], offset);

		} // end for each bone
	} // end for each geometry
}

/** Loads a model without drawing it.
 *
 * @param modelFilename The filename of the model.
 *
 * @param textureDirname The directory that the model's textures are
 * saved in. If set to NULL, the textures are assumed to be in the
 * same directory as the model is in. If the model has already been
 * drawn/loaded, this parameter is unused.
 *
 * @param program The GLSL program to draw the model with.
 *
 * @param bbox To be filled in with the bounding box of the model
 * (xmin, xmax, ymin, etc). The bounding box may be incorrect if the
 * model includes animation.
 *
 * @return Returns a kuhl_geometry object that can be later drawn. If
 * the model contains multiple meshes, kuhl_geometry will be a linked
 * list (i.e., geom->next will not be NULL).
 */
kuhl_geometry* kuhl_load_model(const char *modelFilename, const char *textureDirname,
                               GLuint program, float bbox[6])
{
	char *newModelFilename = kuhl_find_file(modelFilename);
	// Loads the model from the file and reads in all of the textures:
	const struct aiScene *scene = kuhl_private_assimp_load(newModelFilename, textureDirname);
	if(scene == NULL)
	{
		msg(ERROR, "ASSIMP was unable to import the model '%s'.\n", modelFilename);
		return NULL;
	}

	// Convert the information in aiScene into a kuhl_geometry object.
	float transform[16];
	mat4f_identity(transform);
	kuhl_geometry *ret = kuhl_private_load_model(scene, scene->mRootNode,
	                                             program, transform,
	                                             newModelFilename, textureDirname);

	/* Ensure model shows up in bind pose if the caller doesn't
	 * also call kuhl_update_model(). */
	kuhl_update_model(ret, 0, -1);

	/* Calculate bounding box information for the model */
	float bboxLocal[6];
	kuhl_private_calc_bbox(scene->mRootNode, NULL, scene, bboxLocal);
	float min[3],max[3],ctr[3];
	vec3f_set(min, bboxLocal[0], bboxLocal[2], bboxLocal[4]);
	vec3f_set(max, bboxLocal[1], bboxLocal[3], bboxLocal[5]);
	vec3f_add_new(ctr, min, max);
	vec3f_scalarDiv(ctr, 2);

	/* Print bounding box information to stout */
	msg(DEBUG, "%s: bbox min: %10.3f %10.3f %10.3f", modelFilename, min[0], min[1], min[2]);
	msg(DEBUG, "%s: bbox max: %10.3f %10.3f %10.3f", modelFilename, max[0], max[1], max[2]);
	msg(DEBUG, "%s: bbox ctr: %10.3f %10.3f %10.3f", modelFilename, ctr[0], ctr[1], ctr[2]);

	/* If the user requested bounding box information, give it to
	 * them. */
	if(bbox != NULL)
	{
		for(int i=0; i<6; i++)
			bbox[i] = bboxLocal[i];
	}
	return ret;
}
#endif // KUHL_UTIL_USE_ASSIMP


/* Create a matrix scale+translation matrix which shrinks the model to
 * fit into a 1x1x1 box.
 *
 * @param result The resulting transformation matrix.
 *
 * @param bbox The bounding box information (xmin, xmax, ymin, etc.)
 *
 * @param sitOnXZPlane If 1, the box will be translated so that the
 * model sits on the XZ plane.
 */
void kuhl_bbox_fit(float result[16], const float bbox[6], int sitOnXZPlane)
{
	/* Calculate the width/height/depth of the bounding box and
	 * determine which one of the three is the largest. Then, scale
	 * the scene by 1/(largest value) to ensure that it fits in our
	 * view frustum. */
	float min[3], max[3], ctr[3];
	vec3f_set(min, bbox[0], bbox[2], bbox[4]);
	vec3f_set(max, bbox[1], bbox[3], bbox[5]);
	vec3f_add_new(ctr, min, max);
	vec3f_scalarDiv(ctr, 2);

	/* Figure out which dimension is the biggest part of the box */
	float width  = max[0]-min[0];
	float height = max[1]-min[1];
	float depth  = max[2]-min[2];
	float biggestSize = width;
	if(height > biggestSize)
		biggestSize = height;
	if(depth > biggestSize)
		biggestSize = depth;

	float scaleBoundBox[16], moveToOrigin[16];
	/* Scale matrix */
	mat4f_scale_new(scaleBoundBox, 1.0f/biggestSize, 1.0f/biggestSize, 1.0f/biggestSize);
	//printf("Scaling by factor %f\n", 1.0/biggestSize); 

	/* Translate matrix */
	if(sitOnXZPlane == 0)
		mat4f_translate_new(moveToOrigin, -ctr[0], -ctr[1], -ctr[2]); // move to origin
	else
		/* Place the bounding box on top of the origin */
		mat4f_translate_new(moveToOrigin, -ctr[0], -ctr[1]+height/2.0f, -ctr[2]);

	mat4f_mult_mat4f_new(result, scaleBoundBox, moveToOrigin);
}


/* Creates a new framebuffer object (with a depth buffer) that we can
 * render to and therefore render directly to a texture.
 *
 * @param width The width of the framebuffer to create
 *
 * @param height The height of the framebuffer to create
 *
 * @param texture To be filled with a texture ID which the color 
 * buffer of the framebuffer will be connected to. Use NULL if you
 * you do not want the colorbuffer connected to a texture.
 *
 * @param depthTexture To be filled with a texture ID which the
 * depth buffer values of the framebuffer will be connected to. Use
 * NULL if you do not want the depthbuffer connected to a texture.
 *
 * @return Returns a framebuffer id that can be enabled with
 * glBindFramebuffer().
 */
GLint kuhl_gen_framebuffer(int width, int height, GLuint *texture, GLuint *depthTexture)
{
	GLint origBoundTexture,origBoundFrameBuffer,origBoundRenderBuffer;;
	glGetIntegerv(GL_TEXTURE_BINDING_2D,   &origBoundTexture);
	glGetIntegerv(GL_FRAMEBUFFER_BINDING,  &origBoundFrameBuffer);
	glGetIntegerv(GL_RENDERBUFFER_BINDING, &origBoundRenderBuffer);

	GLint maxTextureSize;
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
	if(width < 0 || width > maxTextureSize ||
	   height< 0 || height> maxTextureSize)
	{
		msg(ERROR, "Requested %d x %d texture but maximum allowed is %d\n",
		            width, height, maxTextureSize);
		exit(EXIT_FAILURE);
	}
	
	// set up texture
	if(texture != NULL)
	{
		glGenTextures(1, texture);
		glBindTexture(GL_TEXTURE_2D, *texture);
		glTexImage2D(GL_TEXTURE_2D, 0,GL_RGB, width, height, 0, GL_RGB,
		             GL_UNSIGNED_BYTE, 0);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	}
	if(depthTexture != NULL)
	{
		glGenTextures(1, depthTexture);
		glBindTexture(GL_TEXTURE_2D, *depthTexture);
		glTexImage2D(GL_TEXTURE_2D, 0,GL_DEPTH24_STENCIL8, width, height, 0,
		             GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, 0);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	}

	// set up frame buffer object (FBO)
	GLuint framebuffer = 0;
	glGenFramebuffers(1, &framebuffer);
	glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

	// setup depth buffer
	GLuint depthbuffer = 0;
	glGenRenderbuffers(1, &depthbuffer);
	glBindRenderbuffer(GL_RENDERBUFFER, depthbuffer);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);

	// Connect FBO to depth buffer
	glFramebufferRenderbuffer(GL_FRAMEBUFFER,
	                          GL_DEPTH_STENCIL_ATTACHMENT,
	                          GL_RENDERBUFFER,
	                          depthbuffer);

	if(texture != NULL)
	{
		// Connect FBO to texture
		glFramebufferTexture2D(GL_FRAMEBUFFER,
		                       GL_COLOR_ATTACHMENT0,
		                       GL_TEXTURE_2D,
		                       *texture,      // texture id
		                       0);            // mipmap level
	}
	else
		glDrawBuffer(GL_NONE);

	kuhl_errorcheck();

	if(depthTexture != NULL)
	{
		glFramebufferTexture2D(GL_FRAMEBUFFER,
		                       GL_DEPTH_STENCIL_ATTACHMENT,
		                       GL_TEXTURE_2D,
		                       *depthTexture,      // texture id
		                       0);            // mipmap level
	}
	kuhl_errorcheck();

	GLenum fbStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if(fbStatus != GL_FRAMEBUFFER_COMPLETE)
	{
		printf("%s: glCheckFramebufferStatus() indicated a the following problem with the framebuffer:\n", __func__);
		switch(fbStatus)
		{
			case GL_FRAMEBUFFER_UNDEFINED:
				printf("%s: GL_FRAMEBUFFER_UNDEFINED\n", __func__);
				break;
			case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
				printf("%s: GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT\n", __func__);
				break;
			case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
				printf("%s: GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT\n", __func__);
				break;
			case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER:
				printf("%s: GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER\n", __func__);
				break;
			case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER:
				printf("%s: GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER\n", __func__);
				break;
			case GL_FRAMEBUFFER_UNSUPPORTED:
				printf("%s: GL_FRAMEBUFFER_UNSUPPORTED\n", __func__);
				break;
			case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE:
				printf("%s: GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE\n", __func__);
				break;
			case GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS:
				printf("%s: GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS\n", __func__);
				break;
			default:
				printf("%s: Unknown error.\n", __func__);
				break;
		}
		exit(EXIT_FAILURE);
	}
	kuhl_errorcheck();

	// Restore binding
	glBindTexture(GL_TEXTURE_2D, origBoundTexture);
	glBindFramebuffer(GL_FRAMEBUFFER, origBoundFrameBuffer);
	glBindRenderbuffer(GL_RENDERBUFFER, origBoundRenderBuffer);
	kuhl_errorcheck();
	return framebuffer;
}

GLint kuhl_gen_framebuffer_msaa(int width, int height, GLuint *texture, GLuint *depthTexture, GLint samples)
{
	GLint origBoundTexture,origBoundFrameBuffer,origBoundRenderBuffer;;
	glGetIntegerv(GL_TEXTURE_BINDING_2D,   &origBoundTexture);
	glGetIntegerv(GL_FRAMEBUFFER_BINDING,  &origBoundFrameBuffer);
	glGetIntegerv(GL_RENDERBUFFER_BINDING, &origBoundRenderBuffer);
	kuhl_errorcheck();

	GLint maxSamples;
	glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
	if(samples > GL_MAX_SAMPLES || samples < 1)
	{
		msg(ERROR, "Requested %d samples but maximum allowed is %d\n", samples,
		            maxSamples);
		exit(EXIT_FAILURE);
	}
	GLint maxTextureSize;
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
	if(width < 0 || width > maxTextureSize ||
	   height< 0 || height> maxTextureSize)
	{
		msg(ERROR, "Requested %d x %d texture but maximum allowed is %d\n",
		            width, height, maxTextureSize);
		exit(EXIT_FAILURE);
	}
		
	
	// set up texture
	if(texture != NULL)
	{
		glGenTextures(1, texture);
		glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, *texture);
		glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, samples, GL_RGB8,
		                        width, height, GL_TRUE);
		kuhl_errorcheck();
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	}
	kuhl_errorcheck();
	if(depthTexture != NULL)
	{
		glGenTextures(1, depthTexture);
		glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, *depthTexture);
		glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, samples, GL_DEPTH24_STENCIL8, width, height, GL_TRUE);
		kuhl_errorcheck();
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	}
	kuhl_errorcheck();

	// set up frame buffer object (FBO)
	GLuint framebuffer = 0;
	glGenFramebuffers(1, &framebuffer);
	glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

	// setup depth buffer
	GLuint depthbuffer = 0;
	glGenRenderbuffers(1, &depthbuffer);
	glBindRenderbuffer(GL_RENDERBUFFER, depthbuffer);
	glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH24_STENCIL8, width, height);
	
	// Connect FBO to depth buffer
	glFramebufferRenderbuffer(GL_FRAMEBUFFER,
	                          GL_DEPTH_STENCIL_ATTACHMENT,
	                          GL_RENDERBUFFER,
	                          depthbuffer);
	kuhl_errorcheck();
	if(texture != NULL)
	{
		// Connect FBO to texture
		glFramebufferTexture2D(GL_FRAMEBUFFER,
		                       GL_COLOR_ATTACHMENT0,
		                       GL_TEXTURE_2D_MULTISAMPLE,
		                       *texture,      // texture id
		                       0);            // mipmap level
		kuhl_errorcheck();
	}
	else
		glDrawBuffer(GL_NONE);

	kuhl_errorcheck();

	if(depthTexture != NULL)
	{
		glFramebufferTexture2D(GL_FRAMEBUFFER,
		                       GL_DEPTH_STENCIL_ATTACHMENT,
		                       GL_TEXTURE_2D_MULTISAMPLE,
		                       *depthTexture,      // texture id
		                       0);            // mipmap level
	}
	kuhl_errorcheck();

	GLenum fbStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if(fbStatus != GL_FRAMEBUFFER_COMPLETE)
	{
		msg(ERROR, "glCheckFramebufferStatus() indicated a the following problem with the framebuffer:\n");
		switch(fbStatus)
		{
			case GL_FRAMEBUFFER_UNDEFINED:
				printf("%s: GL_FRAMEBUFFER_UNDEFINED\n", __func__);
				break;
			case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
				printf("%s: GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT\n", __func__);
				break;
			case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
				printf("%s: GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT\n", __func__);
				break;
			case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER:
				printf("%s: GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER\n", __func__);
				break;
			case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER:
				printf("%s: GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER\n", __func__);
				break;
			case GL_FRAMEBUFFER_UNSUPPORTED:
				printf("%s: GL_FRAMEBUFFER_UNSUPPORTED\n", __func__);
				break;
			case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE:
				printf("%s: GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE\n", __func__);
				break;
			case GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS:
				printf("%s: GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS\n", __func__);
				break;
			default:
				printf("%s: Unknown error.\n", __func__);
				break;
		}
		exit(EXIT_FAILURE);
	}
	kuhl_errorcheck();

	// Restore binding
	glBindTexture(GL_TEXTURE_2D, origBoundTexture);
	glBindFramebuffer(GL_FRAMEBUFFER, origBoundFrameBuffer);
	glBindRenderbuffer(GL_RENDERBUFFER, origBoundRenderBuffer);
	kuhl_errorcheck();
	return framebuffer;
}



/** Plays an audio files asynchronously. This method of playing sounds
    is far from ideal, is not efficient, and will only work on
    Linux. However, it is a quick and easy method that does not make
    our code rely on any additional libraries.

    @param filename The filename to play.
 */
void kuhl_play_sound(const char *filename)
{
#ifdef __linux__
	int forkRet = fork();
	if(forkRet == -1) // if fork() error
		perror("fork");
	else if(forkRet == 0) // if child
	{
		/* A Linux-only way for child to ask to receive a SIGHUP
		 * signal when parent dies/exits. */
		prctl(PR_SET_PDEATHSIG, SIGHUP);

		if(strlen(filename) > 4 && !strcasecmp(filename + strlen(filename) - 4, ".wav"))
		{
			/* aplay is a command-line program commonly installed on Linux machines */
			execlp("aplay", "aplay", "--quiet", filename, NULL);
		}
		else if(strlen(filename) > 4 && !strcasecmp(filename + strlen(filename) - 4, ".ogg"))
		{
			/* ogg123 is a command-line program commonly installed on Linux machines */
			execlp("ogg123", "ogg123", "--quiet", filename, NULL);
		}

		/* play is a program that comes with the SoX audio package
		 * that is also commonly installed on Linux systems. It
		 * supports a variety of different file formats. */
		execlp("play", "play", "-q", filename, NULL);

		/* Since exec will never return, we can only get here if exec
		 * failed. */
		perror("execvp");
		msg(FATAL, "Error playing file %s (do you have the aplay, ogg123 and play commands installed on your machine?)\n", filename);
		exit(EXIT_FAILURE);
	}

#else
	msg(ERROR, "This sound function only works on Linux.\n");
#endif
}
