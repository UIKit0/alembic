#include <cstring>
#include <memory>
#include "ProcArgs.h"
#include "PathUtil.h"
#include "SampleUtil.h"
#include "WriteGeo.h"

#include <Alembic/AbcGeom/All.h>
#include <Alembic/AbcCoreHDF5/All.h>


namespace
{

using namespace Alembic::AbcGeom;



void WalkObject( IObject parent, const ObjectHeader &ohead, ProcArgs &args,
             PathList::const_iterator I, PathList::const_iterator E,
                    MatrixSampleMap * xformSamples)
{
    //Accumulate transformation samples and pass along as an argument
    //to WalkObject
    
    IObject nextParentObject;
    
    std::auto_ptr<MatrixSampleMap> concatenatedXformSamples;
    
    if ( IXform::matches( ohead ) )
    {
        if ( args.excludeXform )
        {
            nextParentObject = IObject( parent, ohead.getName() );
        }
        else
        {
            IXform xform( parent, ohead.getName() );
            
            IXformSchema &xs = xform.getSchema();
            
            if ( xs.getNumOps() > 0 )
            { 
                TimeSamplingPtr ts = xs.getTimeSampling();
                size_t numSamples = xs.getNumSamples();
                
                SampleTimeSet sampleTimes;
                GetRelevantSampleTimes( args, ts, numSamples, sampleTimes,
                        xformSamples);
                
                MatrixSampleMap localXformSamples;
                
                MatrixSampleMap * localXformSamplesToFill = 0;
                
                concatenatedXformSamples.reset(new MatrixSampleMap);
                
                if ( !xformSamples )
                {
                    // If we don't have parent xform samples, we can fill
                    // in the map directly.
                    localXformSamplesToFill = concatenatedXformSamples.get();
                }
                else
                {
                    //otherwise we need to fill in a temporary map
                    localXformSamplesToFill = &localXformSamples;
                }
                
                
                for (SampleTimeSet::iterator I = sampleTimes.begin();
                        I != sampleTimes.end(); ++I)
                {
                    XformSample sample = xform.getSchema().getValue(
                            Abc::ISampleSelector(*I));
                    (*localXformSamplesToFill)[(*I)] = sample.getMatrix();
                }
                
                if ( xformSamples )
                {
                    ConcatenateXformSamples(args,
                            *xformSamples,
                            localXformSamples,
                            *concatenatedXformSamples.get());
                }
                
                
                xformSamples = concatenatedXformSamples.get();
                
            }
            
            nextParentObject = xform;
        }
    }
    else if ( ISubD::matches( ohead ) )
    {
        std::string faceSetName;
        
        ISubD subd( parent, ohead.getName() );
        
        //if we haven't reached the end of a specified -objectpath,
        //check to see if the next token is a faceset name.
        //If it is, send the name to ProcessSubD for addition of
        //"face_visibility" tags for the non-matching faces
        if ( I != E )
        {
            if ( subd.getSchema().hasFaceSet( *I ) )
            {
                faceSetName = *I;
            }
        }
        
        ProcessSubD( subd, args, xformSamples, faceSetName );
        
        //if we found a matching faceset, don't traverse below
        if ( faceSetName.empty() )
        {
            nextParentObject = subd;
        }
    }
    else if ( IPolyMesh::matches( ohead ) )
    {
        std::string faceSetName;
        
        IPolyMesh polymesh( parent, ohead.getName() );
        
        //if we haven't reached the end of a specified -objectpath,
        //check to see if the next token is a faceset name.
        //If it is, send the name to ProcessSubD for addition of
        //"face_visibility" tags for the non-matching faces
        if ( I != E )
        {
            if ( polymesh.getSchema().hasFaceSet( *I ) )
            {
                faceSetName = *I;
            }
        }
        
        ProcessPolyMesh( polymesh, args, xformSamples, faceSetName );
        
        //if we found a matching faceset, don't traverse below
        if ( faceSetName.empty() )
        {
            nextParentObject = polymesh;
        }
    }
    else if ( INuPatch::matches( ohead ) )
    {
        INuPatch patch( parent, ohead.getName() );
        // TODO ProcessNuPatch( patch, args );
        
        nextParentObject = patch;
    }
    else if ( IPoints::matches( ohead ) )
    {
        IPoints points( parent, ohead.getName() );
        // TODO ProcessPoints( points, args );
        
        nextParentObject = points;
    }
    else if ( ICurves::matches( ohead ) )
    {
        ICurves curves( parent, ohead.getName() );
        // TODO ProcessCurves( curves, args );
        
        nextParentObject = curves;
    }
    else if ( IFaceSet::matches( ohead ) )
    {
        //don't complain about discovering a faceset upon traversal
    }
    else
    {
        std::cerr << "could not determine type of " << ohead.getName()
                  << std::endl;
        
        std::cerr << ohead.getName() << " has MetaData: "
                  << ohead.getMetaData().serialize() << std::endl;
        
        nextParentObject = parent.getChild(ohead.getName());
    }
    
    if ( nextParentObject.valid() )
    {
        //std::cerr << nextParentObject.getFullName() << std::endl;
        
        if ( I == E )
        {
            for ( size_t i = 0; i < nextParentObject.getNumChildren() ; ++i )
            {
                WalkObject( nextParentObject,
                            nextParentObject.getChildHeader( i ),
                            args, I, E, xformSamples);
            }
        }
        else
        {
            const ObjectHeader *nextChildHeader =
                nextParentObject.getChildHeader( *I );
            
            if ( nextChildHeader != NULL )
            {
                WalkObject( nextParentObject, *nextChildHeader, args, I+1, E,
                    xformSamples);
            }
        }
    }
    
    
    
}

//-*************************************************************************

int ProcInit( struct AtNode *node, void **user_ptr )
{
    ProcArgs * args = new ProcArgs( AiNodeGetStr( node, "data" ) );
    args->proceduralNode = node;
    *user_ptr = args;


    IArchive archive( ::Alembic::AbcCoreHDF5::ReadArchive(),
                      args->filename );

    IObject root = archive.getTop();

    PathList path;
    TokenizePath( args->objectpath, path );

    try
    {
        if ( path.empty() ) //walk the entire scene
        {
            for ( size_t i = 0; i < root.getNumChildren(); ++i )
            {
                WalkObject( root, root.getChildHeader(i), *args,
                            path.end(), path.end(), 0 );
            }
        }
        else //walk to a location + its children
        {
            PathList::const_iterator I = path.begin();

            const ObjectHeader *nextChildHeader =
                    root.getChildHeader( *I );
            if ( nextChildHeader != NULL )
            {
                WalkObject( root, *nextChildHeader, *args, I+1,
                        path.end(), 0);
            }
        }
    }
    catch ( const std::exception &e )
    {
        std::cerr << "exception thrown during ProcInit: "
              << e.what() << std::endl;
    }
    catch (...)
    {
        std::cerr << "exception thrown\n";
    }
    
    
    return 1;
}

//-*************************************************************************

int ProcCleanup( void *user_ptr )
{
    delete reinterpret_cast<ProcArgs*>( user_ptr );
    return 1;
}

//-*************************************************************************

int ProcNumNodes( void *user_ptr )
{
    ProcArgs * args = reinterpret_cast<ProcArgs*>( user_ptr );
    return (int) args->createdNodes.size();
}

//-*************************************************************************

struct AtNode* ProcGetNode(void *user_ptr, int i)
{
    ProcArgs * args = reinterpret_cast<ProcArgs*>( user_ptr );
    
    if ( i >= 0 && i < (int) args->createdNodes.size() )
    {
        return args->createdNodes[i];
    }
    
    return NULL;
}

} //end of anonymous namespace



extern "C"
{
    int ProcLoader(AtProcVtable* api)
    {
        api->Init        = ProcInit;
        api->Cleanup     = ProcCleanup;
        api->NumNodes    = ProcNumNodes;
        api->GetNode     = ProcGetNode;
        strcpy(api->version, AI_VERSION);
        return 1;
    }
}
