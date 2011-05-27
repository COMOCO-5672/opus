/* Copyright (c) 2008-2011 Octasic Inc.
   Written by Jean-Marc Valin */
/*
   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

   - Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

   - Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR
   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/


#include "mlp_train.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <semaphore.h>
#include <pthread.h>
#include <time.h>
#include <signal.h>

int stopped = 0;

void handler(int sig)
{
	stopped = 1;
	signal(sig, handler);
}

MLPTrain * mlp_init(int *topo, int nbLayers, float *inputs, float *outputs, int nbSamples)
{
	int i, j, k;
	MLPTrain *net;
	int inDim, outDim;
	net = malloc(sizeof(*net));
	net->topo = malloc(nbLayers*sizeof(net->topo[0]));
	for (i=0;i<nbLayers;i++)
		net->topo[i] = topo[i];
	inDim = topo[0];
	outDim = topo[nbLayers-1];
	net->in_rate = malloc((inDim+1)*sizeof(net->in_rate[0]));
	net->weights = malloc((nbLayers-1)*sizeof(net->weights));
	for (i=0;i<nbLayers-1;i++)
		net->weights[i] = malloc((topo[i]+1)*topo[i+1]*sizeof(net->weights[0][0]));
	double inMean[inDim];
	for (j=0;j<inDim;j++)
	{
		double std=0;
		inMean[j] = 0;
		for (i=0;i<nbSamples;i++)
		{
			inMean[j] += inputs[i*inDim+j];
			std += inputs[i*inDim+j]*inputs[i*inDim+j];
		}
		inMean[j] /= nbSamples;
		std /= nbSamples;
		net->in_rate[1+j] = .5/(.0001+std);
		std = std-inMean[j]*inMean[j];
		if (std<.001)
			std = .001;
		std = 1/sqrt(inDim*std);
		for (k=0;k<topo[1];k++)
			net->weights[0][k*(topo[0]+1)+j+1] = randn(.7*std);
	}
	net->in_rate[0] = 1;
	for (j=0;j<topo[1];j++)
	{
		double sum = 0;
		for (k=0;k<inDim;k++)
			sum += inMean[k]*net->weights[0][j*(topo[0]+1)+k+1];
		net->weights[0][j*(topo[0]+1)] = -sum;
	}
	for (j=0;j<outDim;j++)
	{
		double mean = 0;
		double std;
		for (i=0;i<nbSamples;i++)
			mean += outputs[i*outDim+j];
		mean /= nbSamples;
		std = 1/sqrt(topo[nbLayers-2]);
		net->weights[nbLayers-2][j*(topo[nbLayers-2]+1)] = mean;
		for (k=0;k<topo[nbLayers-2];k++)
			net->weights[nbLayers-2][j*(topo[nbLayers-2]+1)+k+1] = randn(std);
	}
	return net;
}

#define MAX_NEURONS 100

double compute_gradient(MLPTrain *net, float *inputs, float *outputs, int nbSamples, double *W0_grad, double *W1_grad, double *error_rate)
{
	int i,j;
	int s;
	int inDim, outDim, hiddenDim;
	int *topo;
	double *W0, *W1;
	double rms=0;
	int W0_size, W1_size;
	double hidden[MAX_NEURONS];
	double netOut[MAX_NEURONS];
	double error[MAX_NEURONS];

        *error_rate = 0;
	topo = net->topo;
	inDim = net->topo[0];
	hiddenDim = net->topo[1];
	outDim = net->topo[2];
	W0_size = (topo[0]+1)*topo[1];
	W1_size = (topo[1]+1)*topo[2];
	W0 = net->weights[0];
	W1 = net->weights[1];
	memset(W0_grad, 0, W0_size*sizeof(double));
	memset(W1_grad, 0, W1_size*sizeof(double));
	for (i=0;i<outDim;i++)
		netOut[i] = outputs[i];
	for (s=0;s<nbSamples;s++)
	{
		float *in, *out;
		in = inputs+s*inDim;
		out = outputs + s*outDim;
		for (i=0;i<hiddenDim;i++)
		{
			double sum = W0[i*(inDim+1)];
			for (j=0;j<inDim;j++)
				sum += W0[i*(inDim+1)+j+1]*in[j];
			hidden[i] = tansig_approx(sum);
		}
		for (i=0;i<outDim;i++)
		{
			double sum = W1[i*(hiddenDim+1)];
			for (j=0;j<hiddenDim;j++)
				sum += W1[i*(hiddenDim+1)+j+1]*hidden[j];
			netOut[i] = tansig_approx(sum);
			error[i] = out[i] - netOut[i];
			rms += error[i]*error[i];
                        *error_rate += fabs(error[i])>.5;
		}
		/* Back-propagate error */
		for (i=0;i<outDim;i++)
		{
                        float grad = 1-netOut[i]*netOut[i];
			W1_grad[i*(hiddenDim+1)] += error[i]*grad;
			for (j=0;j<hiddenDim;j++)
				W1_grad[i*(hiddenDim+1)+j+1] += grad*error[i]*hidden[j];
		}
		for (i=0;i<hiddenDim;i++)
		{
			double grad;
			grad = 0;
			for (j=0;j<outDim;j++)
				grad += error[j]*W1[j*(hiddenDim+1)+i+1];
			grad *= 1-hidden[i]*hidden[i];
			W0_grad[i*(inDim+1)] += grad;
			for (j=0;j<inDim;j++)
				W0_grad[i*(inDim+1)+j+1] += grad*in[j];
		}
	}
	return rms;
}

#define NB_THREADS 4

sem_t sem_begin[NB_THREADS];
sem_t sem_end[NB_THREADS];

struct GradientArg {
	int id;
	int done;
	MLPTrain *net;
	float *inputs;
	float *outputs;
	int nbSamples;
	double *W0_grad;
	double *W1_grad;
	double rms;
	double error_rate;
};

void *gradient_thread_process(void *_arg)
{
	int W0_size, W1_size;
	struct GradientArg *arg = _arg;
	int *topo = arg->net->topo;
	W0_size = (topo[0]+1)*topo[1];
	W1_size = (topo[1]+1)*topo[2];
	double W0_grad[W0_size];
	double W1_grad[W1_size];
	arg->W0_grad = W0_grad;
	arg->W1_grad = W1_grad;
	while (1)
	{
		sem_wait(&sem_begin[arg->id]);
		if (arg->done)
			break;
		arg->rms = compute_gradient(arg->net, arg->inputs, arg->outputs, arg->nbSamples, arg->W0_grad, arg->W1_grad, &arg->error_rate);
		sem_post(&sem_end[arg->id]);
	}
	fprintf(stderr, "done\n");
	return NULL;
}

float mlp_train_backprop(MLPTrain *net, float *inputs, float *outputs, int nbSamples, int nbEpoch, float rate)
{
	int i, j;
	int e;
	float last_rms = 1e10;
	int inDim, outDim, hiddenDim;
	int *topo;
	double *W0, *W1;
	double *W0_old, *W1_old;
	double *W0_old2, *W1_old2;
	double *W0_grad, *W1_grad;
	double *W0_oldgrad, *W1_oldgrad;
	double *W0_rate, *W1_rate;
	int W0_size, W1_size;
	topo = net->topo;
	W0_size = (topo[0]+1)*topo[1];
	W1_size = (topo[1]+1)*topo[2];
	struct GradientArg args[NB_THREADS];
	pthread_t thread[NB_THREADS];
	int samplePerPart = nbSamples/NB_THREADS;
	topo = net->topo;
	inDim = net->topo[0];
	hiddenDim = net->topo[1];
	outDim = net->topo[2];
	W0 = net->weights[0];
	W1 = net->weights[1];
	W0_old = malloc(W0_size*sizeof(double));
	W1_old = malloc(W1_size*sizeof(double));
	W0_old2 = malloc(W0_size*sizeof(double));
	W1_old2 = malloc(W1_size*sizeof(double));
	W0_grad = malloc(W0_size*sizeof(double));
	W1_grad = malloc(W1_size*sizeof(double));
	W0_oldgrad = malloc(W0_size*sizeof(double));
	W1_oldgrad = malloc(W1_size*sizeof(double));
	W0_rate = malloc(W0_size*sizeof(double));
	W1_rate = malloc(W1_size*sizeof(double));
	memcpy(W0_old, W0, W0_size*sizeof(double));
	memcpy(W0_old2, W0, W0_size*sizeof(double));
	memset(W0_grad, 0, W0_size*sizeof(double));
	memset(W0_oldgrad, 0, W0_size*sizeof(double));
	memcpy(W1_old, W1, W1_size*sizeof(double));
	memcpy(W1_old2, W1, W1_size*sizeof(double));
	memset(W1_grad, 0, W1_size*sizeof(double));
	memset(W1_oldgrad, 0, W1_size*sizeof(double));
	
	rate /= nbSamples;
	for (i=0;i<hiddenDim;i++)
		for (j=0;j<inDim+1;j++)
			W0_rate[i*(inDim+1)+j] = rate*net->in_rate[j];
	for (i=0;i<W1_size;i++)
		W1_rate[i] = rate;
	
	for (i=0;i<NB_THREADS;i++)
	{
		args[i].net = net;
		args[i].inputs = inputs+i*samplePerPart*inDim;
		args[i].outputs = outputs+i*samplePerPart*outDim;
		args[i].nbSamples = samplePerPart;
		args[i].id = i;
		args[i].done = 0;
		sem_init(&sem_begin[i], 0, 0);
		sem_init(&sem_end[i], 0, 0);
		pthread_create(&thread[i], NULL, gradient_thread_process, &args[i]);
	}
	for (e=0;e<nbEpoch;e++)
	{
		double rms=0;
                double error_rate = 0;
		for (i=0;i<NB_THREADS;i++)
		{
			sem_post(&sem_begin[i]);
		}
		memset(W0_grad, 0, W0_size*sizeof(double));
		memset(W1_grad, 0, W1_size*sizeof(double));
		for (i=0;i<NB_THREADS;i++)
		{
			sem_wait(&sem_end[i]);
			rms += args[i].rms;
			error_rate += args[i].error_rate;
			for (j=0;j<W0_size;j++)
				W0_grad[j] += args[i].W0_grad[j];
			for (j=0;j<W1_size;j++)
				W1_grad[j] += args[i].W1_grad[j];
		}

		float mean_rate = 0, min_rate = 1e10;
		rms = sqrt(rms/(outDim*nbSamples));
		error_rate = (error_rate/(outDim*nbSamples));
		fprintf (stderr, "%f (%f) ", error_rate, last_rms);
		for (i=0;i<W0_size;i++)
		{
			if (W0_oldgrad[i]*W0_grad[i] >= 0)
				W0_rate[i] *= 1.01;
			else
				W0_rate[i] *= .9;
			mean_rate += W0_rate[i];
			if (W0_rate[i] < min_rate)
				min_rate = W0_rate[i];
			if (W0_rate[i] < 1e-15)
				W0_rate[i] = 1e-15;
			W0_oldgrad[i] = W0_grad[i];
			W0_old2[i] = W0_old[i];
			W0_old[i] = W0[i];
			W0[i] += W0_grad[i]*W0_rate[i];
		}
		for (i=0;i<W1_size;i++)
		{
			if (W1_oldgrad[i]*W1_grad[i] >= 0)
				W1_rate[i] *= 1.01;
			else
				W1_rate[i] *= .9;
			mean_rate += W1_rate[i];
			if (W1_rate[i] < min_rate)
				min_rate = W1_rate[i];
			if (W1_rate[i] < 1e-15)
				W1_rate[i] = 1e-15;
			W1_oldgrad[i] = W1_grad[i];
			W1_old2[i] = W1_old[i];
			W1_old[i] = W1[i];
			W1[i] += W1_grad[i]*W1_rate[i];
		}
		if (rms < last_rms)
			last_rms = rms;
		mean_rate /= (topo[0]+1)*topo[1] + (topo[1]+1)*topo[2];
		fprintf (stderr, "%g (min %g) %d\n", mean_rate, min_rate, e);
		if (stopped)
			break;
	}
	for (i=0;i<NB_THREADS;i++)
	{
		args[i].done = 1;
		sem_post(&sem_begin[i]);
		pthread_join(thread[i], NULL);
		fprintf (stderr, "joined %d\n", i);
	}
	free(W0_old);
	free(W1_old);
	free(W0_grad);
	free(W1_grad);
	free(W0_rate);
	free(W1_rate);
	return last_rms;
}

int main(int argc, char **argv)
{
	int i, j;
	int nbInputs;
	int nbOutputs;
	int nbHidden;
	int nbSamples;
	int nbEpoch;
	int nbRealInputs;
	unsigned int seed;
	int ret;
	float rms;
	float *inputs;
	float *outputs;
	if (argc!=6)
	{
		fprintf (stderr, "usage: mlp_train <inputs> <hidden> <outputs> <nb samples> <nb epoch>\n");
		return 1;
	}
	nbInputs = atoi(argv[1]);
	nbHidden = atoi(argv[2]);
	nbOutputs = atoi(argv[3]);
	nbSamples = atoi(argv[4]);
	nbEpoch = atoi(argv[5]);
	nbRealInputs = nbInputs;
	inputs = malloc(nbInputs*nbSamples*sizeof(*inputs));
	outputs = malloc(nbOutputs*nbSamples*sizeof(*outputs));
	
	seed = time(NULL);
	fprintf (stderr, "Seed is %u\n", seed);
	srand(seed);
	build_tansig_table();
	signal(SIGTERM, handler);
	signal(SIGHUP, handler);
	for (i=0;i<nbSamples;i++)
	{
		for (j=0;j<nbRealInputs;j++)
			ret = scanf(" %f", &inputs[i*nbInputs+j]);
		for (j=0;j<nbOutputs;j++)
			ret = scanf(" %f", &outputs[i*nbOutputs+j]);
		if (feof(stdin))
		{
			nbSamples = i;
			break;
		}
	}
	int topo[3] = {nbInputs, nbHidden, nbOutputs};
	MLPTrain *net;

	fprintf (stderr, "Got %d samples\n", nbSamples);
	net = mlp_init(topo, 3, inputs, outputs, nbSamples);
	rms = mlp_train_backprop(net, inputs, outputs, nbSamples, nbEpoch, 1);
	printf ("#include \"mlp.h\"\n\n");
	printf ("/* RMS error was %f, seed was %u */\n\n", rms, seed);
	printf ("static const float weights[%d] = {\n", (topo[0]+1)*topo[1] + (topo[1]+1)*topo[2]);
	printf ("\n/* hidden layer */\n");
	for (i=0;i<(topo[0]+1)*topo[1];i++)
	{
		printf ("%g, ", net->weights[0][i]);
		if (i%5==4)
			printf("\n");
	}
	printf ("\n/* output layer */\n");
	for (i=0;i<(topo[1]+1)*topo[2];i++)
	{
		printf ("%g, ", net->weights[1][i]);
		if (i%5==4)
			printf("\n");
	}
	printf ("};\n\n");
	printf ("static const int topo[3] = {%d, %d, %d};\n\n", topo[0], topo[1], topo[2]);
	printf ("const MLP net = {\n");
	printf ("\t3,\n");
	printf ("\ttopo,\n");
	printf ("\tweights\n};\n");
	return 0;
}
