#ifndef TH_GENERIC_FILE
#define TH_GENERIC_FILE "generic/TemporalConvolution.c"
#else

static int nn_(TemporalConvolution_updateOutput)(lua_State *L)
{
  THTensor *input = luaT_checkudata(L, 2, torch_Tensor);  
  int kW = luaT_getfieldcheckint(L, 1, "kW");
  int dW = luaT_getfieldcheckint(L, 1, "dW");
  int inputFrameSize = luaT_getfieldcheckint(L, 1, "inputFrameSize");
  int outputFrameSize = luaT_getfieldcheckint(L, 1, "outputFrameSize");
  
  THTensor *weight = luaT_getfieldcheckudata(L, 1, "weight", torch_Tensor);
  THTensor *bias = luaT_getfieldcheckudata(L, 1, "bias", torch_Tensor);
  THTensor *output = luaT_getfieldcheckudata(L, 1, "output", torch_Tensor);

  THTensor *outputWindow, *inputWindow;
  int nInputFrame, nOutputFrame;
  long k, i;
  
  int dimS = 0; // sequence dimension
  int dimF = 1; // feature dimension
  
  luaL_argcheck(L, input->nDimension == 2 || input->nDimension == 3, 2, "2D or 3D(batch mode) tensor expected");
  
  if (input->nDimension == 3) 
  {
    dimS = 1;
    dimF = 2;
  }
  luaL_argcheck(L, input->size[dimF] == inputFrameSize, 2, "invalid input frame size");
  luaL_argcheck(L, input->size[dimS] >= kW, 2, "input sequence smaller than kernel size");

  input = THTensor_(newContiguous)(input);
  outputWindow = THTensor_(new)();
  inputWindow = THTensor_(new)();

  nInputFrame = input->size[dimS];
  nOutputFrame = (nInputFrame - kW) / dW + 1;

  if (input->nDimension == 2)
  {
    THTensor_(resize2d)(output,
                        nOutputFrame,
                        outputFrameSize);

    /* bias first */
    for(k = 0; k < nOutputFrame; k++)
    {
      THTensor_(select)(outputWindow, output, 0, k);
      THTensor_(copy)(outputWindow, bias);
    }

    /* ouch */
    for(k = 0; nOutputFrame > 0; k++)
    {
      long outputFrameStride = (kW-1)/dW+1;
      long inputFrameStride = outputFrameStride*dW;
      long nFrame = (nInputFrame-k*dW-kW)/inputFrameStride + 1;
      nOutputFrame -= nFrame;

      THTensor_(setStorage2d)(inputWindow, input->storage,
                              input->storageOffset+k*dW*input->size[1],
                              nFrame, inputFrameStride*input->size[1],
                              kW*input->size[1], 1);

      THTensor_(setStorage2d)(outputWindow, output->storage, 
                              output->storageOffset + k*output->size[1],
                              nFrame, outputFrameStride*output->size[1],
                              output->size[1], 1);

      THTensor_(transpose)(weight, NULL, 0, 1);
      THTensor_(addmm)(outputWindow, 1, outputWindow, 1, inputWindow, weight);
      THTensor_(transpose)(weight, NULL, 0, 1);
    }
  }
  else
  {
    THTensor *outputSample = THTensor_(new)();
    THTensor *inputSample = THTensor_(new)();
    int nBatchFrame = input->size[0];
    
    THTensor_(resize3d)(output,
                        nBatchFrame,
                        nOutputFrame,
                        outputFrameSize);
    
    for(i = 0; i < nBatchFrame; i++)
    {
      THTensor_(select)(outputSample, output, 0, i);
      THTensor_(select)(inputSample, input, 0, i);
      long nOutputSampleFrame = nOutputFrame;
      
      /* bias first */
      for(k = 0; k < nOutputFrame; k++)
      {
        THTensor_(select)(outputWindow, outputSample, 0, k);
        THTensor_(copy)(outputWindow, bias);
      }

      /* ouch */
      for(k = 0; nOutputSampleFrame > 0; k++)
      {
        long outputFrameStride = (kW-1)/dW+1;
        long inputFrameStride = outputFrameStride*dW;
        long nFrame = (nInputFrame-k*dW-kW)/inputFrameStride + 1;
        nOutputSampleFrame -= nFrame;

        THTensor_(setStorage2d)(inputWindow, inputSample->storage,
                                inputSample->storageOffset+k*dW*inputSample->size[1],
                                nFrame, inputFrameStride*inputSample->size[1],
                                kW*inputSample->size[1], 1);

        THTensor_(setStorage2d)(outputWindow, outputSample->storage, 
                                outputSample->storageOffset + k*outputSample->size[1],
                                nFrame, outputFrameStride*outputSample->size[1],
                                outputSample->size[1], 1);

        THTensor_(transpose)(weight, NULL, 0, 1);
        THTensor_(addmm)(outputWindow, 1, outputWindow, 1, inputWindow, weight);
        THTensor_(transpose)(weight, NULL, 0, 1);
      }
    }
    THTensor_(free)(outputSample);
    THTensor_(free)(inputSample);
  }

  THTensor_(free)(outputWindow);
  THTensor_(free)(inputWindow);
  THTensor_(free)(input);

  return 1;
}

static int nn_(TemporalConvolution_updateGradInput)(lua_State *L)
{
  THTensor *input = luaT_checkudata(L, 2, torch_Tensor);  
  THTensor *gradOutput = luaT_checkudata(L, 3, torch_Tensor);  
  int kW = luaT_getfieldcheckint(L, 1, "kW");
  int dW = luaT_getfieldcheckint(L, 1, "dW");
  long nInputFrame;
  long nOutputFrame;

  THTensor *weight = luaT_getfieldcheckudata(L, 1, "weight", torch_Tensor);
  THTensor *gradInput = luaT_getfieldcheckudata(L, 1, "gradInput", torch_Tensor);

  THTensor *gradOutputWindow;
  THTensor *gradInputWindow;
  long k, i;
  
  int dimS = 0; // sequence dimension
  int dimF = 1; // feature dimension
  
  if (gradOutput->nDimension == 3) 
  {
    dimS = 1;
    dimF = 2;
  }
  
  nInputFrame = input->size[dimS];
  nOutputFrame = gradOutput->size[dimS];

  gradOutputWindow = THTensor_(new)();
  gradInputWindow = THTensor_(new)();

  THTensor_(resizeAs)(gradInput, input);
  THTensor_(zero)(gradInput);

  if (gradOutput->nDimension == 2)
  {
    /* ouch */
    for(k = 0; nOutputFrame > 0; k++)
    {
      long outputFrameStride = (kW-1)/dW+1;
      long inputFrameStride = outputFrameStride*dW;
      long nFrame = (nInputFrame-k*dW-kW)/inputFrameStride + 1;
      nOutputFrame -= nFrame;

      THTensor_(setStorage2d)(gradOutputWindow, gradOutput->storage,
                              gradOutput->storageOffset + k*gradOutput->size[1],
                              nFrame, outputFrameStride*gradOutput->size[1],
                              gradOutput->size[1], 1);

      THTensor_(setStorage2d)(gradInputWindow, gradInput->storage,
                              gradInput->storageOffset+k*dW*gradInput->size[1],
                              nFrame, inputFrameStride*gradInput->size[1],
                              kW*gradInput->size[1], 1);

      THTensor_(addmm)(gradInputWindow, 1, gradInputWindow, 1, gradOutputWindow, weight);
    }
  }
  else
  {
    THTensor *gradOutputSample = THTensor_(new)();
    THTensor *gradInputSample = THTensor_(new)();
    int nBatchFrame = input->size[0];
    
    for(i = 0; i < nBatchFrame; i++)
    {
      THTensor_(select)(gradOutputSample, gradOutput, 0, i);
      THTensor_(select)(gradInputSample, gradInput, 0, i);
      int nOutputSampleFrame = nOutputFrame;
      
      /* ouch */
      for(k = 0; nOutputSampleFrame > 0; k++)
      {
        long outputFrameStride = (kW-1)/dW+1;
        long inputFrameStride = outputFrameStride*dW;
        long nFrame = (nInputFrame-k*dW-kW)/inputFrameStride + 1;
        nOutputSampleFrame -= nFrame;

        THTensor_(setStorage2d)(gradOutputWindow, gradOutputSample->storage,
                                gradOutputSample->storageOffset + k*gradOutputSample->size[1],
                                nFrame, outputFrameStride*gradOutputSample->size[1],
                                gradOutputSample->size[1], 1);

        THTensor_(setStorage2d)(gradInputWindow, gradInputSample->storage,
                                gradInputSample->storageOffset+k*dW*gradInputSample->size[1],
                                nFrame, inputFrameStride*gradInputSample->size[1],
                                kW*gradInputSample->size[1], 1);

        THTensor_(addmm)(gradInputWindow, 1, gradInputWindow, 1, gradOutputWindow, weight);
      }
    }
    THTensor_(free)(gradOutputSample);
    THTensor_(free)(gradInputSample);
  }

  THTensor_(free)(gradOutputWindow);
  THTensor_(free)(gradInputWindow);

  return 1;
}

static int nn_(TemporalConvolution_accGradParameters)(lua_State *L)
{
  THTensor *input = luaT_checkudata(L, 2, torch_Tensor);  
  THTensor *gradOutput = luaT_checkudata(L, 3, torch_Tensor);  
  real scale = luaL_optnumber(L, 4, 1);
  int kW = luaT_getfieldcheckint(L, 1, "kW");
  int dW = luaT_getfieldcheckint(L, 1, "dW");
  long nInputFrame;
  long nOutputFrame;

  THTensor *gradWeight = luaT_getfieldcheckudata(L, 1, "gradWeight", torch_Tensor);
  THTensor *gradBias = luaT_getfieldcheckudata(L, 1, "gradBias", torch_Tensor);

  THTensor *gradOutputWindow;
  THTensor *inputWindow;
  long k, i;
  
  int dimS = 0; // sequence dimension
  int dimF = 1; // feature dimension
  
  if (gradOutput->nDimension == 3) 
  {
    dimS = 1;
    dimF = 2;
  }
  
  nInputFrame = input->size[dimS];
  nOutputFrame = gradOutput->size[dimS];

  input = THTensor_(newContiguous)(input);
  gradOutputWindow = THTensor_(new)();
  inputWindow = THTensor_(new)();
  
  if (input->nDimension == 2)
  {
    /* bias first */
    for(k = 0; k < nOutputFrame; k++)
    {
      THTensor_(select)(gradOutputWindow, gradOutput, 0, k);
      THTensor_(cadd)(gradBias, gradBias, scale, gradOutputWindow);
    }

    /* ouch */
    for(k = 0; nOutputFrame > 0; k++)
    {
      long outputFrameStride = (kW-1)/dW+1;
      long inputFrameStride = outputFrameStride*dW;
      long nFrame = (nInputFrame-k*dW-kW)/inputFrameStride + 1;
      nOutputFrame -= nFrame;

      THTensor_(setStorage2d)(inputWindow, input->storage,
                              input->storageOffset+k*dW*input->size[1],
                              nFrame, inputFrameStride*input->size[1],
                              kW*input->size[1], 1);

      THTensor_(setStorage2d)(gradOutputWindow, gradOutput->storage, 
                              gradOutput->storageOffset + k*gradOutput->size[1],
                              nFrame, outputFrameStride*gradOutput->size[1],
                              gradOutput->size[1], 1);

      THTensor_(transpose)(gradOutputWindow, NULL, 0, 1);
      THTensor_(addmm)(gradWeight, 1, gradWeight, scale, gradOutputWindow, inputWindow);
      THTensor_(transpose)(gradOutputWindow, NULL, 0, 1);
    }
  }
  else
  {
    THTensor *gradOutputSample = THTensor_(new)();
    THTensor *inputSample = THTensor_(new)();
    int nBatchFrame = input->size[0];
    
    for(i = 0; i < nBatchFrame; i++)
    {
      THTensor_(select)(gradOutputSample, gradOutput, 0, i);
      THTensor_(select)(inputSample, input, 0, i);
      int nOutputSampleFrame = nOutputFrame;
      
      /* bias first */
      for(k = 0; k < nOutputFrame; k++)
      {
        THTensor_(select)(gradOutputWindow, gradOutputSample, 0, k);
        THTensor_(cadd)(gradBias, gradBias, scale, gradOutputWindow);
      }

      /* ouch */
      for(k = 0; nOutputSampleFrame > 0; k++)
      {
        long outputFrameStride = (kW-1)/dW+1;
        long inputFrameStride = outputFrameStride*dW;
        long nFrame = (nInputFrame-k*dW-kW)/inputFrameStride + 1;
        nOutputSampleFrame -= nFrame;

        THTensor_(setStorage2d)(inputWindow, inputSample->storage,
                                inputSample->storageOffset+k*dW*inputSample->size[1],
                                nFrame, inputFrameStride*inputSample->size[1],
                                kW*inputSample->size[1], 1);

        THTensor_(setStorage2d)(gradOutputWindow, gradOutputSample->storage, 
                                gradOutputSample->storageOffset + k*gradOutputSample->size[1],
                                nFrame, outputFrameStride*gradOutputSample->size[1],
                                gradOutputSample->size[1], 1);

        THTensor_(transpose)(gradOutputWindow, NULL, 0, 1);
        THTensor_(addmm)(gradWeight, 1, gradWeight, scale, gradOutputWindow, inputWindow);
        THTensor_(transpose)(gradOutputWindow, NULL, 0, 1);
      }
    }
    THTensor_(free)(gradOutputSample);
    THTensor_(free)(inputSample);
  }

  THTensor_(free)(gradOutputWindow);
  THTensor_(free)(inputWindow);
  THTensor_(free)(input);

  return 0;
}

static const struct luaL_Reg nn_(TemporalConvolution__) [] = {
  {"TemporalConvolution_updateOutput", nn_(TemporalConvolution_updateOutput)},
  {"TemporalConvolution_updateGradInput", nn_(TemporalConvolution_updateGradInput)},
  {"TemporalConvolution_accGradParameters", nn_(TemporalConvolution_accGradParameters)},
  {NULL, NULL}
};

static void nn_(TemporalConvolution_init)(lua_State *L)
{
  luaT_pushmetatable(L, torch_Tensor);
  luaT_registeratname(L, nn_(TemporalConvolution__), "nn");
  lua_pop(L,1);
}

#endif