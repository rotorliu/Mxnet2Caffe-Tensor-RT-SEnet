#include <assert.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <cmath>
#include <sys/stat.h>
#include <time.h>
#include <cuda_runtime_api.h>
#include <cudnn.h>
#include <cublas_v2.h>
#include <memory>
#include <string.h>
#include <cstdint>
#include <iomanip>

#include "NvInfer.h"
#include "NvCaffeParser.h"
#include "common.h"

#include <sys/time.h>
//#include "plugin.h"

using namespace nvinfer1;
using namespace nvcaffeparser1;
using namespace std;

// stuff we know about the network and the caffe input/output blobs
static const int INPUT_H = 108;
static const int INPUT_W = 108;
static const int INPUT_C = 3;
static const int OUTPUT_SIZE = 192;

static Logger gLogger;

const char* INPUT_BLOB_NAME = "data";
const char* OUTPUT_BLOB_NAME = "flat_192";

template<typename T> void write(char*& buffer, const T& val)
{
    *reinterpret_cast<T*>(buffer) = val;
    buffer += sizeof(T);
}

template<typename T> void read(const char*& buffer, T& val)
{
    val = *reinterpret_cast<const T*>(buffer);
    buffer += sizeof(T);
}



class Broadcast: public IPluginExt{
private:
    int c,h,w;
    string layer_name;
public:
    Broadcast(const char * name):layer_name(name){ 
        printf("init_Layer %s\n",name);
     }
    ~Broadcast(){ }
    
    Broadcast(const char* name,const void* data, size_t length):layer_name(name)
    {
        const char* d = static_cast<const char*>(data), *a = d;
        read(d, c);
        read(d, h);
        read(d, w);
        assert(d == a + length);
    }

    int getNbOutputs() const override{
        return 1;
    }

    Dims getOutputDimensions(int index, const Dims* inputs, int nbInputDims) override
    {
        assert(index == 0 && nbInputDims == 2 && inputs[0].nbDims == 3);
        // assert(mNbInputChannels == inputs[0].d[0] * inputs[0].d[1] * inputs[0].d[2]);
        // return Dims3(mNbOutputChannels, 1, 1);
        printf("%s getOutputDimensions: InputDims = %d %d %d\n",layer_name.c_str(),nbInputDims,inputs[0].nbDims,inputs[1].nbDims);
        printf("inputs[0] -> output  %d %d %d \n",inputs[0].d[0], inputs[0].d[1], inputs[0].d[2]);
        printf("inputs[1] %d %d %d \n",inputs[1].d[0], inputs[1].d[1], inputs[1].d[2]);

        return DimsCHW(inputs[0].d[0], inputs[0].d[1], inputs[0].d[2]);
    }

    bool supportsFormat(DataType type, PluginFormat format) const override { return (type == DataType::kFLOAT || type == DataType::kHALF) && format == PluginFormat::kNCHW; }

   virtual size_t getWorkspaceSize(int maxBatchSize) const override
    {
        return 0;
    }

    void configureWithFormat(const Dims* inputDims, int nbInputs, const Dims* outputDims, int nbOutputs, DataType type, PluginFormat format, int maxBatchSize) override
    {
        assert((type == DataType::kFLOAT || type == DataType::kHALF) && format == PluginFormat::kNCHW);
        c = inputDims[0].d[0];
	    h = inputDims[0].d[1];
	    w = inputDims[0].d[2];
        
    }

    int initialize() override
    {
        return 0;
    }

    virtual int enqueue(int batchSize, const void*const * inputs, void** outputs, void* workspace, cudaStream_t stream) override
    {
    	printf("enqueue %s c  %d  h  %d  w  %d  \n",layer_name.c_str(),c,h,w);

        float *pbottom = (float*)malloc(sizeof(float)*c*h*w);
	    cudaMemcpy((void*)pbottom, inputs[0], sizeof(float) * c* h *w, cudaMemcpyDeviceToHost);

        float *pbottom2 = (float*)malloc(sizeof(float)*c*1*1);
        cudaMemcpy((void*)pbottom2, inputs[1], sizeof(float) * c* 1 *1, cudaMemcpyDeviceToHost);

#if 1
	if(layer_name == "broadcast_mul1")
	{
		ofstream fp("broadcast_mul1.txt");
		for(int i = 0; i < c*h*w; i++)
		{
			fp << pbottom[i] << endl;
		}

        ofstream fp2("reshape1.txt");
		for(int i = 0; i < c; i++)
		{
			fp2 << pbottom2[i] << endl;
		}
        fp.close();
        fp2.close();
	}
#endif

        for(int i=0;i<c;i+=1){
            for(int j=0;j<w*h;j+=1){

                int index = i*h*w;
                if(j==0 && i%50==0){
                    printf("name %s  c: %d i:%d  %f  %f \n ",layer_name.c_str(),i*w*h+j,i,pbottom[index+j],pbottom2[i]);
                }
                
                pbottom[index+j] = pbottom[index+j] *pbottom2[i];
                
            }
        }
        cudaMemcpy(outputs[0], (const void*)pbottom, sizeof(float) * c * h * w,cudaMemcpyHostToDevice);

        if(layer_name == "broadcast_mul1")
                {
                    ofstream fp("broadcast_mul1_out.txt");
                    for(int i = 0; i < c*h*w; i++)
                    {
                        fp << pbottom[i] << endl;
                    }
                }
        
        free(pbottom);
        free(pbottom2);
        return 0;
    }
    virtual void terminate() override {}

 
   virtual size_t getSerializationSize() override
    {
        return 3*sizeof(int);
    }

    virtual void serialize(void* buffer) override
    {
        char* d = static_cast<char*>(buffer), *a = d;

        write(d, c);
        write(d, h);
        write(d, w);
        assert(d == a + getSerializationSize());
    }

};


const char * test_layer = "  ";

class Testlayer: public IPluginExt{
private:
    int c,h,w;
    string layer_name;
public:
    Testlayer(const char * name):layer_name(name){ 
        printf("Testlayer %s\n",name);
     }
    ~Testlayer(){ }
    
    Testlayer(const char* name,const void* data, size_t length):layer_name(name)
    {
    	printf("test_deserialize %s\n",name);
        const char* d = static_cast<const char*>(data), *a = d;

        read(d, c);
        read(d, h);
        read(d, w);
        assert(d == a + length);
    }

    int getNbOutputs() const override{
        return 1;
    }

    Dims getOutputDimensions(int index, const Dims* inputs, int nbInputDims) override
    {
        //assert(index == 0 && nbInputDims == 1 && inputs[0].nbDims == 3);

        printf("%s getOutputDimensions: InputDims = %d %d \n",layer_name.c_str(),nbInputDims,inputs[0].nbDims);
        printf("inputs[0] -> output  %d %d %d \n",inputs[0].d[0], inputs[0].d[1], inputs[0].d[2]);
        //printf("inputs[1] %d %d %d \n",inputs[1].d[0], inputs[1].d[1], inputs[1].d[2]);

        return DimsCHW(inputs[0].d[0], inputs[0].d[1], inputs[0].d[2]);
    }

    bool supportsFormat(DataType type, PluginFormat format) const override { return (type == DataType::kFLOAT || type == DataType::kHALF) && format == PluginFormat::kNCHW; }

   virtual size_t getWorkspaceSize(int maxBatchSize) const override
    {
        return 0;
    }

    void configureWithFormat(const Dims* inputDims, int nbInputs, const Dims* outputDims, int nbOutputs, DataType type, PluginFormat format, int maxBatchSize) override
    {
        assert((type == DataType::kFLOAT || type == DataType::kHALF) && format == PluginFormat::kNCHW);
        c = inputDims[0].d[0];
	    h = inputDims[0].d[1];
	    w = inputDims[0].d[2];
        
    }

    int initialize() override
    {
        return 0;
    }


    virtual int enqueue(int batchSize, const void*const * inputs, void** outputs, void* workspace, cudaStream_t stream) override
    {
    	printf("enqueue %s c  %d h  %d w   %d  \n",layer_name.c_str(),c,h,w);

        float *pbottom = (float*)malloc(sizeof(float)*c*h*w);
	    cudaMemcpy((void*)pbottom, inputs[0], sizeof(float) * c* h *w, cudaMemcpyDeviceToHost);

        // float *pbottom2 = (float*)malloc(sizeof(float)*c*1*1);
        // cudaMemcpy((void*)pbottom2, inputs[1], sizeof(float) * c* 1 *1, cudaMemcpyDeviceToHost);

#if 1
    
    // if(!strcmp(layer_name.c_str(),"reshape0"))
    // {
        ofstream fp;
        fp.open((test_layer),ios::out);

		for(int i = 0; i < c*h*w; i++)
		{
			fp << setiosflags(ios::fixed) <<setprecision(12) << pbottom[i] << endl;
		}
        fp.close(); 

   // }
		
#endif
        cudaMemcpy(outputs[0], (const void*)pbottom, sizeof(float) * c * h * w,cudaMemcpyHostToDevice);
        
        free(pbottom);
        return 0;
    }

    virtual void terminate() override {}
    virtual size_t getSerializationSize() override
    {
        return 3*sizeof(int);
    }

    virtual void serialize(void* buffer) override
    {
        char* d = static_cast<char*>(buffer), *a = d;

        write(d, c);
        write(d, h);
        write(d, w);
        assert(d == a + getSerializationSize());
    }

};


class PluginFactory :public nvinfer1::IPluginFactory, public nvcaffeparser1::IPluginFactoryExt{

public:
   
    std::vector<Broadcast *> broadcast_ptrs;
    std::vector<Testlayer *> testlayer_ptrs;

     bool isPlugin(const char* name) override
    {
        return isPluginExt(name);
    }

    bool isPluginExt(const char* name) override
    {
        return !strncmp(name, "broadcast",9) ||  !strcmp(name,test_layer);
    }

    virtual nvinfer1::IPlugin* createPlugin(const char* layerName, const nvinfer1::Weights* weights, int nbWeights) override
    {
        // there's no way to pass parameters through from the model definition, so we have to define it here explicitly
        
        assert(isPlugin(layerName));
        if(!strncmp(layerName, "broadcast",9)){
            printf("createPlugin_layer_name: %s\n",layerName);

            Broadcast* broadcast_ptr =new Broadcast(layerName);
            broadcast_ptrs.push_back(broadcast_ptr);
            return broadcast_ptr;
        }
        
        else if(!strcmp(layerName,test_layer)){
            printf("test_createPlugin_layer_name: %s\n",layerName);

            Testlayer* testlayer_ptr = new Testlayer(layerName);
            testlayer_ptrs.push_back(testlayer_ptr);
            return testlayer_ptr;
        }

        printf("unknown layer: %s",layerName);
        return nullptr;
    }

    // deserialization plugin implementation
    IPlugin* createPlugin(const char* layerName, const void* serialData, size_t serialLength) override
    {
        assert(isPlugin(layerName));
        if(!strncmp(layerName, "broadcast",9))
        {
            printf("deserialization_layer:  %s\n",layerName);

            Broadcast* broadcast_ptr  =new Broadcast(layerName, serialData, serialLength);
            broadcast_ptrs.push_back(broadcast_ptr);
            return broadcast_ptr;
        }
        
        else if(!strcmp(layerName,test_layer)){
            printf("test_deserialization_layer:  %s\n",layerName);

            Testlayer* testlayer_ptr = new Testlayer(layerName,serialData,serialLength);
            testlayer_ptrs.push_back(testlayer_ptr);
            return testlayer_ptr;
        }
        printf("unknown layer: %s",layerName);
        return nullptr;
    }

    void destroyPlugin()
    {
        for(auto ptr:broadcast_ptrs)
            delete ptr;
        for(auto ptr:testlayer_ptrs)
            delete ptr;
        broadcast_ptrs.clear();
        testlayer_ptrs.clear();
    }

};


void caffeToTRTModel(const std::string& deployFile,                 // name for caffe prototxt
                     const std::string& modelFile,                  // name for model
                     const std::vector<std::string>& outputs,       // network outputs
                     unsigned int maxBatchSize,                     // batch size - NB must be at least as large as the batch we want to run with)
                     nvcaffeparser1::IPluginFactoryExt* pluginFactory, // factory for plugin layers
                     IHostMemory *&trtModelStream)                  // output stream for the TensorRT model
{
    // create the builder
    IBuilder* builder = createInferBuilder(gLogger);

    // parse the caffe model to populate the network, then set the outputs
    INetworkDefinition* network = builder->createNetwork();
    ICaffeParser* parser = createCaffeParser();
    parser->setPluginFactoryExt(pluginFactory);

    // bool fp16 = builder->platformHasFastFp16();
    const IBlobNameToTensor* blobNameToTensor = parser->parse(deployFile.c_str(),
                                                              modelFile.c_str(),
                                                              *network,DataType::kFLOAT);

    // specify which tensors are outputs
    for (auto& s : outputs)
        network->markOutput(*blobNameToTensor->find(s.c_str()));

    // Build the engine
    builder->setMaxBatchSize(maxBatchSize);
    builder->setMaxWorkspaceSize(16 << 20);
    // builder->setFp16Mode(fp16);

    ICudaEngine* engine = builder->buildCudaEngine(*network);
    assert(engine);

    // we don't need the network any more, and we can destroy the parser
    network->destroy();
    parser->destroy();

    // serialize the engine, then close everything down
    trtModelStream = engine->serialize();

    engine->destroy();
    builder->destroy();
    shutdownProtobufLibrary();
}


void doInference(IExecutionContext& context, float* input, float* output, int batchSize)
{
    const ICudaEngine& engine = context.getEngine();
    // input and output buffer pointers that we pass to the engine - the engine requires exactly IEngine::getNbBindings(),
    // of these, but in this case we know that there is exactly one input and one output.
    assert(engine.getNbBindings() == 2);
    void* buffers[2];

    // In order to bind the buffers, we need to know the names of the input and output tensors.
    // note that indices are guaranteed to be less than IEngine::getNbBindings()
    int inputIndex = engine.getBindingIndex(INPUT_BLOB_NAME),
        outputIndex = engine.getBindingIndex(OUTPUT_BLOB_NAME);

    // create GPU buffers and a stream
    CHECK(cudaMalloc(&buffers[inputIndex], batchSize * INPUT_C * INPUT_H * INPUT_W * sizeof(float)));
    CHECK(cudaMalloc(&buffers[outputIndex], batchSize * OUTPUT_SIZE * sizeof(float)));

    cudaStream_t stream;
    CHECK(cudaStreamCreate(&stream));

    // DMA the input to the GPU,  execute the batch asynchronously, and DMA it back:
    CHECK(cudaMemcpyAsync(buffers[inputIndex], input, batchSize * INPUT_C * INPUT_H * INPUT_W * sizeof(float), cudaMemcpyHostToDevice, stream));
    context.enqueue(batchSize, buffers, stream, nullptr);
    CHECK(cudaMemcpyAsync(output, buffers[outputIndex], batchSize * OUTPUT_SIZE*sizeof(float), cudaMemcpyDeviceToHost, stream));
    cudaStreamSynchronize(stream);

    // release the stream and the buffers
    cudaStreamDestroy(stream);
    CHECK(cudaFree(buffers[inputIndex]));
    CHECK(cudaFree(buffers[outputIndex]));
}


#if 0

void read_img(const char* filename,float* input_data,int h,int w){
    cv::Mat img;

    img = cv::imread(filename, -1);
    // img = cv::imread(filename, cv::IMREAD_COLOR);
    assert(img.empty());
    cv::resize(img,img,cv::Size(h,w));
    float *img_data = (float *)img.data;
   
    unsigned int size=h * w;
    const float pixelMean[3]{ 104.0f, 117.0f, 123.0f }; // also in BGR order

    for(int c=0;c<INPUT_C;c++){
        for (unsigned j = 0; j < size; ++j)
            input_data[c*size + j] = float(img_data[c*size + j + 2 - c]) - pixelMean[c];
    }

}
#endif

int main(int argc, char** argv)
{
    PluginFactory parserPluginFactory;

    IHostMemory *trtModelStream{ nullptr };
    const char * mdole_path = "../../../data/be-converted.caffemodel";
    const char * proto_path = "../../../data/be-converted.prototxt";;
    caffeToTRTModel(proto_path, mdole_path, std::vector < std::string > { OUTPUT_BLOB_NAME }, 1, &parserPluginFactory, trtModelStream);
    
    parserPluginFactory.destroyPlugin();
    assert(trtModelStream != nullptr);

    float *data = (float *)malloc( INPUT_C * INPUT_H * INPUT_W *sizeof(float) );;
    const char * img_file = "/home/wjq/TensorRT-4.0.1.6/data/before_forward.jpg";

    //read_img(img_file,data,108,108);
    
    for (int i = 0; i < INPUT_C* INPUT_H * INPUT_W; i++)
        data[i]=1;

    IRuntime* runtime = createInferRuntime(gLogger);
    assert(runtime != nullptr);
    ICudaEngine* engine = runtime->deserializeCudaEngine(trtModelStream->data(), trtModelStream->size(), &parserPluginFactory);
    assert(engine != nullptr);
    trtModelStream->destroy();
    IExecutionContext* context = engine->createExecutionContext();
    assert(context != nullptr);


   // Run inference on input data
    float prob[OUTPUT_SIZE];
    doInference(*context, data, prob, 1);

    // Destroy the engine
    context->destroy();
    engine->destroy();
    runtime->destroy();
    free(data);

    // Print histogram of the output distribution
    std::cout << "\nOutput:\n\n";
    
    ofstream fp("output-108.txt");

    for(int i=0;i<OUTPUT_SIZE;i++){
        fp << setiosflags(ios::fixed) <<setprecision(12) << prob[i] << endl;

        printf("%d %f \t",i,prob[i]);
        if(i%5==0)
            printf("\n");
    }

    std::cout << std::endl;

    return 0;
}