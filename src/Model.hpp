
#ifndef MODEL_HPP
#define MODEL_HPP

#include "CellularSpace.hpp"
#include "Flow.hpp"
#include "MPIImpl.hpp"
#include "assert.h"
#include <stdlib.h>
#include <iostream>
#include <fstream>
using namespace std;

template<class T>
class Model{
public:
    T flow;
    double time;
    double time_step;

    Model(){ }

    Model(const T &flow, const double &time, const double &time_step){
        this->flow = flow;
        this->time = time;
        this->time_step = time_step;
    }

    Model(const Model<T> &model){
        this->flow = model.flow;
        this->time = model.time;
        this->time_step = model.time_step;
    }

    Model<T>& operator=(const Model<T> &model){
        if(this != &model){
            this->flow = model.flow;
            this->time = model.time;
            this->time_step = model.time_step;
        }
        return *this;
    }

    ~Model(){ }

    // missing implement
    double execute(){
        for(double t = 0; t < this->time; t = t + this->time_step){
            this->flow.execute();
        }
    }

    template<class R>
    void execute(const MPI_Comm &mpi_comm, const CellularSpace<R> &cellular_space){
        int comm_size, comm_rank, comm_workers, count, offset;
        MPI_Comm_size(mpi_comm, &comm_size);
        MPI_Comm_rank(mpi_comm, &comm_rank);
        MPI_Status mpi_status;
        MPI_Request mpi_send_request, mpi_recv_request;
        comm_workers = comm_size - 1;

        if(comm_rank == 0){
            count = (cellular_space.height*cellular_space.width)/comm_workers;
            offset = 0;
            int x_init_s, y_init_s, height_s, width_s;
            int index[comm_size];
            char word_cs_send[23];

            // para cada maquina { crie uma regiao (i.e.linhas) do espaço celular }
            for(int dest = 1; dest <= comm_size-1; dest++){
                sprintf(word_cs_send, "%d|%d:%d|%d",
                    offset/cellular_space.width, cellular_space.y_init, cellular_space.height/comm_workers, cellular_space.width);
                MPI_Send(word_cs_send, 23, MPI_CHAR, dest, FROM_MASTER, MPI_COMM_WORLD);
                offset = offset + count;
                index[dest]= offset;
            }

            // Request Flow::execute() from process
            char word_execute_send[23];
            int dest_ = (this->flow.source.x/(cellular_space.height/comm_workers)) + 1;
            sprintf(word_execute_send, "%d|%d:%d|%lf", dest_, this->flow.source.x, this->flow.source.y, this->flow.flow_rate);
            cout << word_execute_send << endl;

            for(int dest = 1; dest <= comm_size-1; dest++){
                MPI_Send(word_execute_send, 23, MPI_CHAR, dest, 999, mpi_comm);
            }

            R temp, acumulated_value_recv = 0;
            for(int source = 1; source < comm_size; source++){
                MPI_Recv(&temp, 1, ConvertType(getAbstractionDataType<R>()), source, source, MPI_COMM_WORLD, &mpi_status);
                acumulated_value_recv += temp;
            }

            // verifica se os valores simulados foram conservados
            assert((acumulated_value_recv - 10000) < 0.001);

            // recebe o dado de conclusao de geracao dos arquivos de cada SLAYER
            char file_output_name_recv[30];
            char str_c[50];
            string line_, str_ = "../output/output ";
            fstream file_output, file_output_recv;

            // definindo o nome do arquivo de saida
            str_ += __TIMESTAMP__;
            str_ += ".txt";
            strcpy(str_c, str_.c_str());


            file_output.open(str_c, fstream::out | fstream::ate);
            for(int source = 1; source <= NWORKERS; source++){
                MPI_Recv(file_output_name_recv, 30, MPI_CHAR, source, source, MPI_COMM_WORLD, &mpi_status);

                file_output_recv.open(file_output_name_recv, fstream::in);

                if(file_output_recv.is_open()){
                    if(file_output.is_open()){
                        while(getline(file_output_recv, line_)){
                            line_ += "\n";
                            file_output << line_;
                        }
                    }else{
                        cout << __FILE__ << ": " << __LINE__ << endl;
                    }
                }else{
                    cout << __FILE__ << ": " << __LINE__ << endl;
                }

                // fechando os arquivos
                file_output_recv.close();
            }
            file_output.close();
        }

        // executa a simulacao nas maquinas slayers
        if(comm_rank != 0){
            char word_cs_recv[23], word_execute_recv[23];

            MPI_Recv(word_cs_recv, 23, MPI_CHAR, MASTER, FROM_MASTER, MPI_COMM_WORLD, &mpi_status);
            char *x_init_c = strtok(word_cs_recv, "|");
            char *y_init_c = strtok(NULL, ":");
            char *height_c = strtok(NULL, "|");
            char *width_c = strtok(NULL, ":");
            int x_init_s = atoi(x_init_c);
            int y_init_s = atoi(y_init_c);
            int height_s = atoi(height_c);
            int width_s = atoi(width_c);


            CellularSpace<R> cs = CellularSpace<R>(x_init_s, y_init_s, height_s, width_s);
            MPI_Aint aint;
            int address = MPI_Address(&cs, &aint);

            // Initializing the neighbors of the cellular space created in each process
            for(int i = 0; i < (cs.height * cs.width); i++){
                cs.memoria[i] = Cell<R>((cs.x_init + (i/cs.width)), (i%cs.width), Attribute<R>(i, 1));
                cs.memoria[i] = cs.memoria[i].SetNeighbor();
            }

            MPI_Recv(word_execute_recv, 23, MPI_CHAR, MASTER, 999, mpi_comm, &mpi_status);
            char *rank_c = strtok(word_execute_recv, "|:");
            char *x_c = strtok(NULL, ":");
            char *y_c = strtok(NULL, "|");
            char *flow_rate_c = strtok(NULL, "|:");
            int rank_ = atoi(rank_c);
            int x_ = atoi(x_c);
            int y_ = atoi(y_c);
            int flow_rate_ = atoi(flow_rate_c);

            // corrigir x_init
            // corrigir endereçamento de cell
            // corrigir acesso a cell
            // subtrair flow
            // incrementar flow

            // maquina que armazena a celula a ser fluxionada executa() o fluxo
            if(comm_rank == rank_){
                cout << cs.memoria[x_*cs.width + y_ - cs.x_init].x << " " << cs.memoria[x_*cs.width + y_ - cs.x_init].y
                    << " " << cs.memoria[x_*cs.width + y_].count_neighbors << endl;

                //for(double t = 0; t < this->time; t = t + this->time_step){
                    this->flow.last_execute = this->flow.execute();
                    cout << comm_rank << ": " << this->flow.execute() << endl;
                // }

                int count_neighbors_send, y_send;
                R last_execute_send;

                // Atualizando o valor do atributo na maquina vizinha
                if(cs.memoria[x_*cs.width + y_ - cs.x_init].x - cs.x_init == PROC_DIMX-1){
                    switch(cs.memoria[x_*cs.width + y_ - cs.x_init].count_neighbors){
                        case 3:
                            cout << __FILE__ << ": " << __LINE__ << endl;
                            break;
                        case 5:
                            cout << __FILE__ << ": " << __LINE__ << endl;
                            break;
                        case 8:
                            count_neighbors_send = 3;
                            last_execute_send = this->flow.last_execute/cs.memoria[x_*cs.width + y_ - cs.x_init].count_neighbors;
                            y_send = cs.memoria[x_*cs.width + y_ - cs.x_init].y - 1;

                            MPI_Send(&count_neighbors_send, 1, MPI_INT, rank_+1, rank_, MPI_COMM_WORLD);
                            MPI_Send(&last_execute_send, 1, ConvertType(getAbstractionDataType<R>()), rank_+1, rank_, MPI_COMM_WORLD);
                            MPI_Send(&y_send, 1, MPI_INT, rank_+1, rank_+10, MPI_COMM_WORLD);

                            cs.memoria[x_*cs.width + y_-1 - cs.x_init].attribute.value += last_execute_send;
                            cs.memoria[x_*cs.width + y_+1 - cs.x_init].attribute.value += last_execute_send;
                            for(int i = 0; i < count_neighbors_send; i++)
                                cs.memoria[(x_-1)*cs.width + y_-1+i - cs.x_init].attribute.value += last_execute_send;

                            cs.memoria[x_*cs.width + y_ - cs.x_init].attribute.value -= this->flow.last_execute;

                            break;
                        default:
                            cout << __FILE__ << ": " << __LINE__ << endl;
                    }
                }

                if(cs.memoria[x_*cs.width + y_ - cs.x_init].x - cs.x_init == PROC_DIMX){
                    cout << __FILE__ << ": " << __LINE__ << endl;
                }
            }

            if(comm_rank == rank_+1){
                int count_neighbors_recv, y_recv;
                R last_execute_recv;

                MPI_Recv(&count_neighbors_recv, 1, MPI_INT, rank_, rank_, MPI_COMM_WORLD, &mpi_status);
                MPI_Recv(&last_execute_recv, 1, ConvertType(getAbstractionDataType<R>()), rank_, rank_, MPI_COMM_WORLD, &mpi_status);
                MPI_Recv(&y_recv, 1, MPI_INT, rank_, rank_+10, MPI_COMM_WORLD, &mpi_status);

                // cout << rank_+1 << ": " << count_neighbors_recv << " " << last_execute_recv << " " << y_recv << endl;
                for(int i = 0; i < count_neighbors_recv; i++)
                    cs.memoria[y_recv + i].attribute.value += last_execute_recv;
            }

            // calcula o somatorio dos valores atributos resultante na particao do espaco celular
            R acumulated_value_send = 0;
            for(int i = 0; i < cs.height*cs.width; i++)
                acumulated_value_send += cs.memoria[i].attribute.value;

            // envia o valor somado para o MASTER, para que o assert() seja feito
            MPI_Send(&acumulated_value_send, 1, ConvertType(getAbstractionDataType<R>()), MASTER, comm_rank, MPI_COMM_WORLD);

            // cada slayer salva seus dados do cellular space sao salvos num arquivo .txt
            fstream file_output;
            char file_output_name[30];

            sprintf(file_output_name, "../output/comm_rank%d.txt", comm_rank);
            file_output.open(file_output_name, fstream::out | fstream::trunc);

            for(int i = 0; i < PROC_DIMX*PROC_DIMY; i++){
                file_output << cs.memoria[i].x << "\t" << cs.memoria[i].y << "\t";
                file_output << cs.memoria[i].attribute.value << endl;
            }

            file_output.close();

            // e envia a conclusao de geracao do arquivo para que a MASTER junte todos os resultados gerados
            MPI_Send(file_output_name, 30, MPI_CHAR, MASTER, comm_rank, MPI_COMM_WORLD);
        }
    }
};

#endif
