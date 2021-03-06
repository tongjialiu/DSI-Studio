#ifndef M_PI
#define M_PI        3.14159265358979323846
#endif
#include "tracking_thread.hpp"
#include "fib_data.hpp"
void ThreadData::push_tracts(std::vector<std::vector<float> >& local_tract_buffer)
{
    std::lock_guard<std::mutex> lock(lock_feed_function);
    pushing_data = true;
    for(unsigned int index = 0;index < local_tract_buffer.size();++index)
    {
        track_buffer.push_back(std::vector<float>());
        track_buffer.back().swap(local_tract_buffer[index]);
    }
    local_tract_buffer.clear();
    pushing_data = false;
}
void ThreadData::end_thread(void)
{
    if (!threads.empty())
    {
        joinning = true;
        for(int i = 0;i < threads.size();++i)
            threads[i]->wait();
        threads.clear();
    }
}

void ThreadData::run_thread(TrackingMethod* method_ptr,unsigned int thread_count,unsigned int thread_id,unsigned int max_count)
{
    std::auto_ptr<TrackingMethod> method(method_ptr);
    std::uniform_real_distribution<float> rand_gen(0,1),
            angle_gen(15.0*M_PI/180.0,90.0*M_PI/180.0),
            smoothing_gen(0.0,0.95),
            step_gen(method->trk.vs[0]*0.1,method->trk.vs[0]),
            threshold_gen(0.5*param.otsu_threshold,0.7*param.otsu_threshold);
    unsigned int iteration = thread_id; // for center seed
    float white_matter_t = method_ptr->param.threshold*1.2;
    if(!roi_mgr.seeds.empty())
    try{
        std::vector<std::vector<float> > local_track_buffer;
        while(!joinning &&
              (!stop_by_tract || tract_count[thread_id] < max_count) &&
              (stop_by_tract || seed_count[thread_id] < max_count) &&
              (max_seed_count == 0 || seed_count[thread_id] < max_seed_count) &&
              (!center_seed || iteration < roi_mgr.seeds.size()))
        {
            if(!pushing_data && (iteration & 0x00000FFF) == 0x00000FFF && !local_track_buffer.empty())
                push_tracts(local_track_buffer);
            if(param.threshold == 0.0)
            {
                method->current_fa_threshold = threshold_gen(seed);
                white_matter_t = method->current_fa_threshold*1.2;
            }
            if(param.cull_cos_angle == 1.0)
                method->current_tracking_angle = std::cos(angle_gen(seed));
            if(param.smooth_fraction == 1.0)
                method->current_tracking_smoothing = smoothing_gen(seed);
            if(param.step_size == 0.0)
            {
                float step_size_in_mm = step_gen(seed);
                method->current_step_size_in_voxel[0] = step_size_in_mm/method->trk.vs[0];
                method->current_step_size_in_voxel[1] = step_size_in_mm/method->trk.vs[1];
                method->current_step_size_in_voxel[2] = step_size_in_mm/method->trk.vs[2];
                method->current_max_steps3 = std::round(3.0*param.max_length/step_size_in_mm);
                method->current_min_steps3 = std::round(3.0*param.min_length/step_size_in_mm);
            }
            ++seed_count[thread_id];
            if(center_seed)
            {
                if(!method->init(initial_direction,
                                 image::vector<3,float>(roi_mgr.seeds[iteration].x(),
                                                        roi_mgr.seeds[iteration].y(),
                                                        roi_mgr.seeds[iteration].z()),
                                 seed))
                {
                    iteration+=thread_count;
                    continue;
                }
                if(initial_direction == 0)// primary direction
                    iteration+=thread_count;
            }
            else
            {
                // this ensure consistency
                std::lock_guard<std::mutex> lock(lock_seed_function);
                iteration+=thread_count;
                unsigned int i = rand_gen(seed)*((float)roi_mgr.seeds.size()-1.0);
                image::vector<3,float> pos;
                pos[0] = (float)roi_mgr.seeds[i].x() + rand_gen(seed)-0.5;
                pos[1] = (float)roi_mgr.seeds[i].y() + rand_gen(seed)-0.5;
                pos[2] = (float)roi_mgr.seeds[i].z() + rand_gen(seed)-0.5;
                if(!method->init(initial_direction,pos,seed))
                    continue;
            }
            unsigned int point_count;
            const float *result = method->tracking(tracking_method,point_count);
            if(!result)
                continue;
            const float* end = result+point_count+point_count+point_count;
            if(check_ending)
            {
                if(point_count < 2)
                    continue;
                image::vector<3> p0(result),p1(result+3),p2(end-6),p3(end-3);
                p1 -= p0;
                p0 -= p1;
                p2 -= p3;
                p3 -= p2;
                if(method->trk.is_white_matter(p0,white_matter_t) ||
                   method->trk.is_white_matter(p3,white_matter_t))
                    continue;
            }
            ++tract_count[thread_id];
            local_track_buffer.push_back(std::vector<float>(result,end));
        }
        push_tracts(local_track_buffer);
    }
    catch(...)
    {

    }
    running[thread_id] = 0;
}

bool ThreadData::fetchTracks(TractModel* handle)
{
    if (track_buffer.empty())
        return false;

    std::lock_guard<std::mutex> lock(lock_feed_function);
    handle->add_tracts(track_buffer);
    track_buffer.clear();
    return true;

}
TrackingMethod* ThreadData::new_method(const tracking_data& trk)
{

    std::auto_ptr<basic_interpolation> interpo_method;
    switch (interpolation_strategy)
    {
    case 0:
        interpo_method.reset(new trilinear_interpolation);
        break;
    case 1:
        interpo_method.reset(new trilinear_interpolation_with_gaussian_basis);
        break;
    case 2:
        interpo_method.reset(new nearest_direction);
        break;

    }
    TrackingMethod* method = new TrackingMethod(trk,interpo_method.release(),roi_mgr,param);
    method->current_fa_threshold = method->param.threshold;
    method->current_tracking_angle = method->param.cull_cos_angle;
    method->current_tracking_smoothing = method->param.smooth_fraction;
    method->current_step_size_in_voxel[0] = method->param.step_size/method->trk.vs[0];
    method->current_step_size_in_voxel[1] = method->param.step_size/method->trk.vs[1];
    method->current_step_size_in_voxel[2] = method->param.step_size/method->trk.vs[2];
    if(method->param.step_size != 0.0)
    {
        method->current_max_steps3 = std::round(3.0*method->param.max_length/method->param.step_size);
        method->current_min_steps3 = std::round(3.0*method->param.min_length/method->param.step_size);
    }
    return method;
}

void ThreadData::run(const tracking_data& trk,
         unsigned int thread_count,
         unsigned int termination_count,
         bool wait)
{
    report.clear();
    report.str("");
    report << " A deterministic fiber tracking algorithm (Yeh et al., PLoS ONE 8(11): e80713) was used."
           << roi_mgr.report;
    if(param.cull_cos_angle != 1.0)
        report << " The angular threshold was " << (int)std::round(std::acos(param.cull_cos_angle)*180/3.1415926) << " degrees.";
    else
        report << " The angular threshold was randomly selected from 15 degrees to 90 degrees.";

    if(param.step_size != 0.0)
        report << " The step size was " << param.step_size << " mm.";
    else
        report << " The step size was randomly selected from 0.1 voxel to 3 voxels.";

    param.otsu_threshold = image::segmentation::otsu_threshold(image::make_image(trk.fa[0],trk.dim));
    if(int(param.threshold*1000) == int(600*param.otsu_threshold))
        report << " The default anisotropy threshold was used.";
    else
    {
        if(param.threshold == 0.0)
            report << " The anisotropy threshold was randomly selected.";
        else
            report << " The anisotropy threshold was " << param.threshold << ".";
    }

    if(param.smooth_fraction != 0.0)
    {
        if(param.smooth_fraction != 1.0)
            report << " The fiber trajectories were smoothed by averaging the propagation direction with "
                   << (int)std::round(param.smooth_fraction * 100.0) << "% of the previous direction.";
        else
            report << " The fiber trajectories were smoothed by averaging the propagation direction with \
a percentage of the previous direction. The percentage was randomly selected from 0% to 95%.";
    }
    report << " Tracks with length shorter than " << param.min_length << " or longer than " << param.max_length  << " mm were discarded.";

    if(!termination_count)
        return;
    // to ensure consistency, seed initialization with all orientation only fits with single thread
    if(initial_direction == 2)
        thread_count = 1;

    if(center_seed)
    {
        std::srand(0);
        std::random_shuffle(roi_mgr.seeds.begin(),roi_mgr.seeds.end());
    }
    seed_count.clear();
    tract_count.clear();
    seed_count.resize(thread_count);
    tract_count.resize(thread_count);
    running.resize(thread_count);
    pushing_data = false;
    std::fill(running.begin(),running.end(),1);

    end_thread();
    if(thread_count > termination_count)
        thread_count = termination_count;
    if(thread_count < 1)
        thread_count = 1;
    unsigned int run_count = std::max<int>(1,termination_count/thread_count);
    unsigned int total_run_count = 0;
    for (unsigned int index = 0;index < thread_count-1;++index,total_run_count += run_count)
        threads.push_back(std::make_shared<std::future<void> >(std::async(std::launch::async,
                [this,&trk,thread_count,index,run_count](){run_thread(new_method(trk),thread_count,index,run_count);})));


    if(wait)
    {
        run_thread(new_method(trk),thread_count,thread_count-1,termination_count-total_run_count);
        for(int i = 0;i < threads.size();++i)
            threads[i]->wait();
    }
    else
        threads.push_back(std::make_shared<std::future<void> >(std::async(std::launch::async,
                [this,&trk,thread_count,termination_count,total_run_count](){run_thread(new_method(trk),thread_count,thread_count-1,termination_count-total_run_count);})));

    report << " A total of " << termination_count << (stop_by_tract ? " tracts were calculated.":" seeds were placed.");
}
