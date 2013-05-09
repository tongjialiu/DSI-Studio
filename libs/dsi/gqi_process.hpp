#ifndef DDI_PROCESS_HPP
#define DDI_PROCESS_HPP
#define _USE_MATH_DEFINES
#include <math.h>
#include <boost/math/special_functions/sinc.hpp>
#include "basic_process.hpp"
#include "basic_voxel.hpp"
#include "image_model.hpp"

class CorrectB0  : public BaseProcess
{
public:
    std::vector<unsigned int> b0_images;
public:
    virtual void init(Voxel& voxel)
    {
        b0_images.clear();
        for(unsigned int index = 0;index < voxel.bvalues.size();++index)
            if(voxel.bvalues[index] == 0)
                b0_images.push_back(index);
    }
    virtual void run(Voxel& voxel, VoxelData& data)
    {
        //average all the b0 iamges
        if((b0_images.size() == 1 && !voxel.half_sphere) || b0_images.empty())
            return;
        float sum_b0 = data.space[b0_images.front()];
        if(b0_images.size() >= 2)
        {
            for(unsigned int index = 1;index < b0_images.size();++index)
            {
                    sum_b0 += data.space[b0_images[index]];
                    data.space[b0_images[index]] = 0;
            }
            sum_b0 /= b0_images.size();
        }
        if(voxel.half_sphere)
                sum_b0 /= 2.0;
        data.space[b0_images.front()] = sum_b0;
    }
};


class QSpace2Odf  : public BaseProcess
{
public:
    std::vector<float> sinc_ql;
    double base_function(double theta)
    {
        if(std::abs(theta) < 0.000001)
            return 1.0/3.0;
        return (2*std::cos(theta)+(theta-2.0/theta)*std::sin(theta))/theta/theta;
    }
public:
    virtual void init(Voxel& voxel)
    {
        unsigned int odf_size = voxel.ti.half_vertices_count;
        sinc_ql.resize(odf_size*voxel.bvalues.size());
        float sigma = voxel.param[0]; //optimal 1.24

        // calculate reconstruction matrix
        for (unsigned int j = 0,index = 0; j < odf_size; ++j)
            for (unsigned int i = 0; i < voxel.bvalues.size(); ++i,++index)
                sinc_ql[index] = voxel.bvectors[i]*
                             image::vector<3,float>(voxel.ti.vertices[j])*
                               std::sqrt(voxel.bvalues[i]*0.01506); // �^G�_

        for (unsigned int index = 0; index < sinc_ql.size(); ++index)
            sinc_ql[index] = voxel.r2_weighted ?
                         base_function(sinc_ql[index]*sigma):
                         boost::math::sinc_pi(sinc_ql[index]*sigma);
    }
    virtual void run(Voxel& voxel, VoxelData& data)
    {
        image::matrix::vector_product(&*sinc_ql.begin(),&*data.space.begin(),&*data.odf.begin(),
                                    image::dyndim(data.odf.size(),voxel.bvalues.size()));
    }
    virtual void end(Voxel& voxel,MatFile& mat_writer)
    {

    }
};



#endif//DDI_PROCESS_HPP
