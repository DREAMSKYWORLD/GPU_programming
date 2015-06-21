#include <wb.h>
#include <algorithm>
#include <sstream>

#define wbCheck(stmt)                                                          \
  do {                                                                         \
    cudaError_t err = stmt;                                                    \
    if (err != cudaSuccess) {                                                  \
      wbLog(ERROR, "Failed to run stmt ", #stmt);                              \
      wbLog(ERROR, "Got CUDA error ...  ", cudaGetErrorString(err));           \
      return -1;                                                               \
    }                                                                          \
  } while (0)

const unsigned TILE_WIDTH = 16;

void printMatrix(float *m, int numRows, int numColumns)
{
   for (int i = 0; i < numRows; ++i)
   {
      std::stringstream str;
      for (int j = 0; j < numColumns; ++j)
      {
         str << m[i * numColumns + j] << " ";
      }
      wbLog(TRACE, str.str().c_str());
   }  
}

// Compute C = A * B
__global__ void matrixMultiply(float *A, float *B, float *C, 
                               int numARows, int numAColumns, 
                               int numBRows, int numBColumns,
                               int numCRows, int numCColumns) 
{
  //@@ Insert code to implement matrix multiplication here
  __shared__ float ds_A[TILE_WIDTH][TILE_WIDTH];
  __shared__ float ds_B[TILE_WIDTH][TILE_WIDTH];
  int bx = blockIdx.x; 
  int by = blockIdx.y;
  int tx = threadIdx.x; 
  int ty = threadIdx.y;
  int Col = bx * blockDim.x + tx;
  int Row = by * blockDim.y + ty;
  float Cvalue = 0.0;

  int n = numAColumns;
  for (int t = 0; t < (n - 1) / TILE_WIDTH + 1; ++t) 
  {
     // Collaborative loading of A and B tiles into shared memory
     int A_y_coord = Row;
     int A_x_coord = t * TILE_WIDTH + tx;
     if ( (A_y_coord < numCRows) && (A_x_coord < n) )
        ds_A[ty][tx] = A[A_y_coord * n + A_x_coord];
     else
        ds_A[ty][tx] = 0.0;

     int B_y_coord = t * TILE_WIDTH + ty;
     int B_x_coord = Col;
     if ( (B_x_coord < numCColumns) && (B_y_coord < n) )
        ds_B[ty][tx] = B[B_y_coord * numCColumns + B_x_coord];
     else
        ds_B[ty][tx] = 0.0;

     __syncthreads();

     for (int i = 0; i < TILE_WIDTH; ++i)
        Cvalue += ds_A[ty][i] * ds_B[i][tx];

     __syncthreads();
  }

  if ((Row < numCRows) && (Col < numCColumns))
     C[Row * numCColumns + Col] = Cvalue;
}

int main(int argc, char **argv) 
{
  wbArg_t args;
  float *hostA; // The A matrix
  float *hostB; // The B matrix
  float *hostC; // The output C matrix
  float *deviceA;
  float *deviceB;
  float *deviceC;
  int numARows;    // number of rows in the matrix A
  int numAColumns; // number of columns in the matrix A
  int numBRows;    // number of rows in the matrix B
  int numBColumns; // number of columns in the matrix B
  int numCRows;    // number of rows in the matrix C (you have to set this)
  int numCColumns; // number of columns in the matrix C (you have to set this)

  args = wbArg_read(argc, argv);

  wbTime_start(Generic, "Importing data and creating memory on host");
  hostA = ( float * )wbImport(wbArg_getInputFile(args, 0), &numARows, &numAColumns);
  hostB = ( float * )wbImport(wbArg_getInputFile(args, 1), &numBRows, &numBColumns);

  //@@ Set numCRows and numCColumns
  numCRows = numARows;
  numCColumns = numBColumns;
  
  int sizeA = numARows * numAColumns * sizeof(float);
  int sizeB = numBRows * numBColumns * sizeof(float);
  int sizeC = numCRows * numCColumns * sizeof(float);

  //@@ Allocate the hostC matrix
  hostC = ( float * )malloc(sizeC);

  wbTime_stop(Generic, "Importing data and creating memory on host");

  wbLog(TRACE, "The dimensions of A are ", numARows, " x ", numAColumns);
  wbLog(TRACE, "The dimensions of B are ", numBRows, " x ", numBColumns);
  wbLog(TRACE, "The dimensions of C are ", numCRows, " x ", numCColumns);

  wbTime_start(GPU, "Allocating GPU memory.");
  //@@ Allocate GPU memory here
  wbCheck(cudaMalloc((void**) &deviceA, sizeA));
  wbCheck(cudaMalloc((void**) &deviceB, sizeB));
  wbCheck(cudaMalloc((void**) &deviceC, sizeC));

  wbTime_stop(GPU, "Allocating GPU memory.");

  wbTime_start(GPU, "Copying input memory to the GPU.");
  //@@ Copy memory to the GPU here
  wbCheck(cudaMemcpy(deviceA, hostA, sizeA, cudaMemcpyHostToDevice));
  wbCheck(cudaMemcpy(deviceB, hostB, sizeB, cudaMemcpyHostToDevice));

  wbTime_stop(GPU, "Copying input memory to the GPU.");

  //@@ Initialize the grid and block dimensions here
  dim3 dimGrid( (numCColumns - 1) / TILE_WIDTH + 1, (numCRows - 1) / TILE_WIDTH + 1, 1);
  dim3 dimBlock(TILE_WIDTH, TILE_WIDTH, 1);

  wbTime_start(Compute, "Performing CUDA computation");
  //@@ Launch the GPU Kernel here
  matrixMultiply<<<dimGrid, dimBlock>>>(deviceA, deviceB, deviceC, 
                                        numARows, numAColumns, 
                                        numBRows, numBColumns, 
                                        numCRows, numCColumns);

  cudaDeviceSynchronize();
  wbTime_stop(Compute, "Performing CUDA computation");

  wbTime_start(Copy, "Copying output memory to the CPU");
  //@@ Copy the GPU memory back to the CPU here
  wbCheck(cudaMemcpy(hostC, deviceC, sizeC, cudaMemcpyDeviceToHost));

  wbTime_stop(Copy, "Copying output memory to the CPU");

  wbTime_start(GPU, "Freeing GPU Memory");
  //@@ Free the GPU memory here
  wbCheck(cudaFree(deviceA));
  wbCheck(cudaFree(deviceB));
  wbCheck(cudaFree(deviceC));

  wbTime_stop(GPU, "Freeing GPU Memory");

  wbSolution(args, hostC, numCRows, numCColumns);

  free(hostA);
  free(hostB);
  free(hostC);

  return 0;
}
