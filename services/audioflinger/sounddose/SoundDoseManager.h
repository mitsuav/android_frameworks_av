/*
**
** Copyright 2022, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#pragma once

#include <android/media/BnSoundDose.h>
#include <android/media/ISoundDoseCallback.h>
#include <audio_utils/MelAggregator.h>
#include <audio_utils/MelProcessor.h>
#include <binder/Status.h>
#include <mutex>
#include <unordered_map>

namespace android {

class SoundDoseManager : public audio_utils::MelProcessor::MelCallback {
  public:
    /** CSD is computed with a rolling window of 7 days. */
    static constexpr int64_t kCsdWindowSeconds = 604800;  // 60s * 60m * 24h * 7d
    /** Default RS2 value in dBA as defined in IEC 62368-1 3rd edition. */
    static constexpr float kDefaultRs2Value = 100.f;

    SoundDoseManager()
        : mMelAggregator(sp<audio_utils::MelAggregator>::make(kCsdWindowSeconds)),
          mRs2Value(kDefaultRs2Value){};

    /**
     * \brief Creates or gets the MelProcessor assigned to the streamHandle
     *
     * \param deviceId          id for the devices where the stream is active.
     * \param streanHandle      handle to the stream
     * \param sampleRate        sample rate for the processor
     * \param channelCount      number of channels to be processed.
     * \param format            format of the input samples.
     *
     * \return MelProcessor assigned to the stream and device id.
     */
    sp<audio_utils::MelProcessor> getOrCreateProcessorForDevice(audio_port_handle_t deviceId,
                                                                audio_io_handle_t streamHandle,
                                                                uint32_t sampleRate,
                                                                size_t channelCount,
                                                                audio_format_t format);

    /**
     * \brief Removes stream processor when MEL computation is not needed anymore
     *
     * \param streanHandle      handle to the stream
     */
    void removeStreamProcessor(audio_io_handle_t streamHandle);

    /**
     * Sets the output RS2 value for momentary exposure warnings. Must not be
     * higher than 100dBA and not lower than 80dBA.
     *
     * \param rs2Value value to use for momentary exposure
     */
    void setOutputRs2(float rs2Value);

    /**
     * \brief Registers the interface for passing callbacks to the AudioService and gets
     * the ISoundDose interface.
     *
     * \returns the sound dose binder to send commands to the SoundDoseManager
     **/
    sp<media::ISoundDose> getSoundDoseInterface(const sp<media::ISoundDoseCallback>& callback);

    std::string dump() const;

    // used for testing
    size_t getCachedMelRecordsSize() const;
    bool useFrameworkMel() const;
    bool computeCsdOnAllDevices() const;


    /** Method for converting from audio_utils::CsdRecord to media::SoundDoseRecord. */
    static media::SoundDoseRecord csdRecordToSoundDoseRecord(const audio_utils::CsdRecord& legacy);

    // ------ Override audio_utils::MelProcessor::MelCallback ------
    void onNewMelValues(const std::vector<float>& mels, size_t offset, size_t length,
                        audio_port_handle_t deviceId) const override;

    void onMomentaryExposure(float currentMel, audio_port_handle_t deviceId) const override;

private:
    class SoundDose : public media::BnSoundDose,
                      public IBinder::DeathRecipient {
    public:
        SoundDose(SoundDoseManager* manager, const sp<media::ISoundDoseCallback>& callback)
            : mSoundDoseManager(manager),
              mSoundDoseCallback(callback) {};

        /** IBinder::DeathRecipient. Listen to the death of ISoundDoseCallback. */
        virtual void binderDied(const wp<IBinder>& who);

        /** BnSoundDose override */
        binder::Status setOutputRs2(float value) override;
        binder::Status resetCsd(float currentCsd,
                                const std::vector<media::SoundDoseRecord>& records) override;
        binder::Status getOutputRs2(float* value);
        binder::Status getCsd(float* value);
        binder::Status forceUseFrameworkMel(bool useFrameworkMel);
        binder::Status forceComputeCsdOnAllDevices(bool computeCsdOnAllDevices);

        wp<SoundDoseManager> mSoundDoseManager;
        const sp<media::ISoundDoseCallback> mSoundDoseCallback;
    };

    void resetSoundDose();

    void resetCsd(float currentCsd, const std::vector<media::SoundDoseRecord>& records);

    sp<media::ISoundDoseCallback> getSoundDoseCallback() const;

    void setUseFrameworkMel(bool useFrameworkMel);
    void setComputeCsdOnAllDevices(bool computeCsdOnAllDevices);

    mutable std::mutex mLock;

    // no need for lock since MelAggregator is thread-safe
    const sp<audio_utils::MelAggregator> mMelAggregator;

    std::unordered_map<audio_io_handle_t, wp<audio_utils::MelProcessor>> mActiveProcessors
            GUARDED_BY(mLock);

    float mRs2Value GUARDED_BY(mLock);

    sp<SoundDose> mSoundDose GUARDED_BY(mLock);

    bool mUseFrameworkMel GUARDED_BY(mLock);
    bool mComputeCsdOnAllDevices GUARDED_BY(mLock);
};

}  // namespace android